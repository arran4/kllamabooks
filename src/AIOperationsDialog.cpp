#include "AIOperationsDialog.h"

#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QSplitter>
#include <QScrollArea>

#include "AIOperationsManager.h"
#include "ModelSelectionDialog.h"
#include <QMessageBox>

AIOperationsDialog::AIOperationsDialog(BookDatabase* db, const QString& defaultPrompt,
                                       const QList<OllamaModelInfo>& modelInfos,
                                       const QStringList& fallbackModels, QWidget* parent)
    : QDialog(parent), m_db(db), m_modelInfos(modelInfos), m_fallbackModels(fallbackModels) {
    Q_ASSERT(m_db != nullptr);
    setWindowTitle(tr("AI Document Operations"));
    resize(500, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_mainSplitter = new QSplitter(Qt::Vertical, this);

    // Top section: Operation and Prompt Instructions
    QWidget* topWidget = new QWidget(m_mainSplitter);
    QVBoxLayout* topLayout = new QVBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);

    // Operation
    QHBoxLayout* opLayout = new QHBoxLayout();
    opLayout->addWidget(new QLabel(tr("Operation:")));
    m_operationCombo = new QComboBox(this);

    QList<AIOperation> ops = AIOperationsManager::getMergedOperations(db);
    for (const AIOperation& op : ops) {
        m_operationCombo->addItem(op.name, op.id);
        m_operationCombo->setItemData(m_operationCombo->count() - 1, op.prompt, Qt::UserRole + 1);
    }

    opLayout->addWidget(m_operationCombo, 1);
    topLayout->addLayout(opLayout);

    // Prompt
    QHBoxLayout* promptLabelLayout = new QHBoxLayout();
    promptLabelLayout->addWidget(new QLabel(tr("Prompt Instructions:")));
    promptLabelLayout->addStretch();

    QPushButton* saveDraftBtn = new QPushButton(tr("Save Draft"), this);
    QPushButton* restoreDraftBtn = new QPushButton(tr("Restore Draft"), this);
    promptLabelLayout->addWidget(saveDraftBtn);
    promptLabelLayout->addWidget(restoreDraftBtn);
    topLayout->addLayout(promptLabelLayout);

    connect(saveDraftBtn, &QPushButton::clicked, this, &AIOperationsDialog::onSaveDraftClicked);
    connect(restoreDraftBtn, &QPushButton::clicked, this, &AIOperationsDialog::onRestoreDraftClicked);

    m_promptEdit = new QTextEdit(this);
    m_promptEdit->setPlainText(defaultPrompt);
    topLayout->addWidget(m_promptEdit);

    m_mainSplitter->addWidget(topWidget);

    // Bottom section: Dynamic inputs scroll area
    QScrollArea* scrollArea = new QScrollArea(m_mainSplitter);
    scrollArea->setWidgetResizable(true);
    QWidget* bottomWidget = new QWidget();
    QVBoxLayout* bottomLayout = new QVBoxLayout(bottomWidget);
    m_dynamicInputsLayout = new QFormLayout();
    bottomLayout->addLayout(m_dynamicInputsLayout);
    bottomLayout->addStretch();
    scrollArea->setWidget(bottomWidget);

    m_mainSplitter->addWidget(scrollArea);

    // Set stretch factors so the text edit takes most of the space initially
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 1);

    mainLayout->addWidget(m_mainSplitter);

    // Models Selection
    QHBoxLayout* modelsLayout = new QHBoxLayout();
    modelsLayout->addWidget(new QLabel(tr("Model(s):"), this));
    m_selectModelsBtn = new QPushButton(this);
    modelsLayout->addWidget(m_selectModelsBtn);
    mainLayout->addLayout(modelsLayout);

    // Init models
    QStringList availableNames;
    for (const auto& mi : m_modelInfos) availableNames.append(mi.name);
    if (availableNames.isEmpty()) availableNames = m_fallbackModels;

    QString dbModel = m_db->getSetting("book", 0, "defaultModel", "");
    if (!dbModel.isEmpty() && availableNames.contains(dbModel)) {
        m_selectedModels << dbModel;
    } else {
        QSettings settings;
        QString systemModel = settings.value("systemModel", "").toString();
        if (!systemModel.isEmpty() && availableNames.contains(systemModel)) {
            m_selectedModels << systemModel;
        } else if (!availableNames.isEmpty()) {
            m_selectedModels << availableNames.first();
        }
    }

    updateModelButtonText();
    connect(m_selectModelsBtn, &QPushButton::clicked, this, &AIOperationsDialog::onSelectModelsClicked);


    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    QPushButton* generateBtn = new QPushButton(tr("Generate"), this);
    generateBtn->setDefault(true);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(generateBtn);
    mainLayout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(generateBtn, &QPushButton::clicked, this, &QDialog::accept);

    // Auto-update prompt when operation changes
    connect(m_operationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        QString prompt = m_operationCombo->itemData(index, Qt::UserRole + 1).toString();
        m_promptEdit->setPlainText(prompt);
    });

    // Set initial prompt if no default provided
    if (defaultPrompt.isEmpty() && m_operationCombo->count() > 0) {
        m_promptEdit->setPlainText(m_operationCombo->itemData(0, Qt::UserRole + 1).toString());
    }

    connect(m_promptEdit, &QTextEdit::textChanged, this, &AIOperationsDialog::updateDynamicInputs);
    updateDynamicInputs();
}

AIOperationsDialog::~AIOperationsDialog() {}

QString AIOperationsDialog::getOperation() const { return m_operationCombo->currentData().toString(); }

QString AIOperationsDialog::getPrompt() const { return m_finalPrompt; }

QStringList AIOperationsDialog::getSelectedModels() const { return m_selectedModels; }

void AIOperationsDialog::onSaveDraftClicked() {
    if (!m_db) return;
    QString prompt = m_promptEdit->toPlainText();
    m_db->setSetting("ai_operations", 0, "draft_prompt", prompt);
    QMessageBox::information(this, tr("Draft Saved"), tr("Prompt instructions draft saved successfully."));
}

void AIOperationsDialog::onRestoreDraftClicked() {
    if (!m_db) return;
    QString prompt = m_db->getSetting("ai_operations", 0, "draft_prompt", "");
    if (!prompt.isEmpty()) {
        m_promptEdit->setPlainText(prompt);
    } else {
        QMessageBox::information(this, tr("No Draft"), tr("No saved draft found."));
    }
}

void AIOperationsDialog::updateModelButtonText() {
    if (m_selectedModels.isEmpty()) {
        m_selectModelsBtn->setText(tr("Select Model(s)"));
    } else if (m_selectedModels.size() == 1) {
        m_selectModelsBtn->setText(m_selectedModels.first());
    } else {
        m_selectModelsBtn->setText(tr("%1 Models Selected").arg(m_selectedModels.size()));
    }
}

void AIOperationsDialog::onSelectModelsClicked() {
    ModelSelectionDialog dlg(m_modelInfos, m_fallbackModels, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_selectedModels = dlg.selectedModels();
        updateModelButtonText();
    }
}

void AIOperationsDialog::setForkOnlyMode(bool enabled) {
    if (enabled) {
        m_operationCombo->setEnabled(false);
    } else {
        m_operationCombo->setEnabled(true);
    }
}

void AIOperationsDialog::updateDynamicInputs() {
    QString prompt = m_promptEdit->toPlainText();

    QList<DynamicInputInfo> newInputs;

    QRegularExpression re("\\{(input|textarea)(?:\\s+\"([^\"]+)\")?\\}");
    QRegularExpressionMatchIterator i = re.globalMatch(prompt);

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString type = match.captured(1);
        QString label = match.captured(2);
        QString fullMatch = match.captured(0);

        if (label.isEmpty()) {
            int start = match.capturedStart(0);
            int lineStart = start > 0 ? prompt.lastIndexOf('\n', start - 1) + 1 : 0;
            QString preceding = prompt.mid(lineStart, start - lineStart).trimmed();
            if (!preceding.isEmpty()) {
                label = preceding;
            } else {
                int end = match.capturedEnd(0);
                int nextLineStart = prompt.indexOf('\n', end);
                if (nextLineStart != -1) {
                    int nextLineEnd = prompt.indexOf('\n', nextLineStart + 1);
                    if (nextLineEnd == -1) nextLineEnd = prompt.length();
                    QString nextLine = prompt.mid(nextLineStart + 1, nextLineEnd - nextLineStart - 1).trimmed();
                    if (!nextLine.isEmpty() && nextLine.length() < 100) {
                        label = nextLine;
                    }
                }
            }
            if (label.isEmpty()) {
                label = "Input";
            }
        }

        newInputs.append({fullMatch, type, label, nullptr});
    }

    // Check if the required inputs have changed
    bool changed = (m_currentDynamicInputs.size() != newInputs.size());
    if (!changed) {
        for (int j = 0; j < newInputs.size(); ++j) {
            if (m_currentDynamicInputs[j].type != newInputs[j].type ||
                m_currentDynamicInputs[j].label != newInputs[j].label) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        // Clear layout
        QLayoutItem* item;
        while ((item = m_dynamicInputsLayout->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget()) {
                w->deleteLater();
            }
            delete item;
        }
        m_currentDynamicInputs.clear();

        for (int j = 0; j < newInputs.size(); ++j) {
            DynamicInputInfo& info = newInputs[j];
            QWidget* w = nullptr;
            if (info.type == "input") {
                w = new QLineEdit(this);
            } else if (info.type == "textarea") {
                QTextEdit* te = new QTextEdit(this);
                te->setMaximumHeight(60);
                w = te;
            }

            if (w) {
                QLabel* labelWidget = new QLabel(info.label + ":", this);
                m_dynamicInputsLayout->addRow(labelWidget, w);
                info.widget = w;
                m_currentDynamicInputs.append(info);
            }
        }
    }
}

void AIOperationsDialog::accept() {
    QString prompt = m_promptEdit->toPlainText();

    QRegularExpression re("\\{(input|textarea)(?:\\s+\"([^\"]+)\")?\\}");
    QRegularExpressionMatchIterator i = re.globalMatch(prompt);

    struct MatchInfo {
        int start;
        int length;
    };
    QList<MatchInfo> matchInfos;
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        matchInfos.append({static_cast<int>(match.capturedStart(0)), static_cast<int>(match.capturedLength(0))});
    }

    if (matchInfos.size() == m_currentDynamicInputs.size()) {
        for (int j = matchInfos.size() - 1; j >= 0; --j) {
            const MatchInfo& minfo = matchInfos[j];
            const DynamicInputInfo& dinfo = m_currentDynamicInputs[j];

            QString val;
            if (QLineEdit* le = qobject_cast<QLineEdit*>(dinfo.widget)) {
                val = le->text();
            } else if (QTextEdit* te = qobject_cast<QTextEdit*>(dinfo.widget)) {
                val = te->toPlainText();
            }
            prompt.replace(minfo.start, minfo.length, val);
        }
    }

    m_finalPrompt = prompt;
    QDialog::accept();
}
