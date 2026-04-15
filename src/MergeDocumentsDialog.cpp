#include "MergeDocumentsDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QSettings>

#include <QUuid>
#include <QInputDialog>

#include "ModelSelectionDialog.h"
#include "TemplateParser.h"

MergeDocumentsDialog::MergeDocumentsDialog(BookDatabase* db, const QList<int>& documentIds, const QList<OllamaModelInfo>& modelInfos, const QStringList& fallbackModels, QWidget* parent)
    : QDialog(parent), m_db(db), m_documentIds(documentIds), m_modelInfos(modelInfos), m_fallbackModels(fallbackModels) {
    setWindowTitle(tr("Merge Documents with AI"));
    setMinimumWidth(600);
    setMinimumHeight(500);

    // Fetch document contents
    QList<DocumentNode> docs = m_db->getDocuments(-1);
    for (int id : m_documentIds) {
        for (const auto& doc : docs) {
            if (doc.id == id) {
                m_documentContents.append(doc.content);
                m_documentTitles.append(doc.title);
                break;
            }
        }
    }

    QVBoxLayout* layout = new QVBoxLayout(this);

    // Action combo box
    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->addWidget(new QLabel(tr("Action:"), this));
    m_actionTypeCombo = new QComboBox(this);
    actionLayout->addWidget(m_actionTypeCombo);
    layout->addLayout(actionLayout);

    // We populate the combo box in setIsRegenerating(),
    // we default to false.
    setIsRegenerating(false);

    // Title input
    QHBoxLayout* titleLayout = new QHBoxLayout();
    titleLayout->addWidget(new QLabel(tr("Title:"), this));
    m_titleEdit = new QLineEdit(this);
    titleLayout->addWidget(m_titleEdit);
    layout->addLayout(titleLayout);

    connect(m_titleEdit, &QLineEdit::textEdited, this, [this](const QString&) {
        m_titleManuallyEdited = true;
    });

    // Templates selection
    QHBoxLayout* topLayout = new QHBoxLayout();
    topLayout->addWidget(new QLabel(tr("Merge Template:"), this));
    m_templateCombo = new QComboBox(this);
    topLayout->addWidget(m_templateCombo);

    QPushButton* saveTemplateBtn = new QPushButton(tr("Save as Template"), this);
    topLayout->addWidget(saveTemplateBtn);
    layout->addLayout(topLayout);

    connect(saveTemplateBtn, &QPushButton::clicked, this, &MergeDocumentsDialog::onSaveTemplateClicked);

    QHBoxLayout* instructionLayout = new QHBoxLayout();
    QLabel* instructionLabel = new QLabel(tr("Prompt Configuration:"), this);
    instructionLayout->addWidget(instructionLabel);

    QPushButton* previewBtn = new QPushButton(tr("Preview"), this);
    instructionLayout->addWidget(previewBtn);
    connect(previewBtn, &QPushButton::clicked, this, &MergeDocumentsDialog::onPreviewClicked);

    QPushButton* helpBtn = new QPushButton("?", this);
    helpBtn->setFixedWidth(30);
    helpBtn->setToolTip(tr("Show template help"));
    instructionLayout->addWidget(helpBtn);
    instructionLayout->addStretch();
    layout->addLayout(instructionLayout);

    connect(helpBtn, &QPushButton::clicked, this, &MergeDocumentsDialog::onHelpClicked);

    m_instructionEdit = new QTextEdit(this);
    m_instructionEdit->setPlaceholderText(tr("Use {foreach contexts}...{between}...{end} and {input \"label\"} placeholders..."));
    m_instructionEdit->setAcceptRichText(false);
    layout->addWidget(m_instructionEdit);

    // Dynamic Inputs
    m_dynamicInputsLayout = new QFormLayout();
    layout->addLayout(m_dynamicInputsLayout);

    // Models Selection
    QHBoxLayout* modelsLayout = new QHBoxLayout();
    modelsLayout->addWidget(new QLabel(tr("Model(s):"), this));
    m_selectModelsBtn = new QPushButton(this);
    modelsLayout->addWidget(m_selectModelsBtn);
    layout->addLayout(modelsLayout);

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

    if (m_selectedModels.isEmpty()) {
        m_selectModelsBtn->setText(tr("Select Model(s)"));
    } else if (m_selectedModels.size() == 1) {
        m_selectModelsBtn->setText(m_selectedModels.first());
    } else {
        m_selectModelsBtn->setText(tr("%1 Models Selected").arg(m_selectedModels.size()));
    }
    connect(m_selectModelsBtn, &QPushButton::clicked, this, &MergeDocumentsDialog::onSelectModelsClicked);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &MergeDocumentsDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    loadTemplates();

    connect(m_templateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MergeDocumentsDialog::onTemplateChanged);
    connect(m_instructionEdit, &QTextEdit::textChanged, this, &MergeDocumentsDialog::updateDynamicInputs);

    if (m_templateCombo->count() > 0) {
        onTemplateChanged(0);
    } else {
        m_instructionEdit->setPlainText(tr("Merge the following documents into one coherent document:\n\n{foreach contexts}\nDocument:\n{context}\n{between}\n---\n{end}"));
    }
}

void MergeDocumentsDialog::loadTemplates() {
    QList<AIOperation> allOps = AIOperationsManager::getMergedOperations(m_db);
    m_templates.clear();

    m_templateCombo->addItem(tr("Default Merge"));
    for (const auto& op : allOps) {
        if (op.inputType == "multiple" || op.inputType == "any") {
            m_templates.append(op);
            m_templateCombo->addItem(op.name, op.id);
        }
    }
}

void MergeDocumentsDialog::onHelpClicked() {
    QString helpText = tr(
        "<b>Template Language Help</b><br><br>"
        "You can configure how the selected documents are merged using template tags.<br><br>"
        "<b><code>{foreach contexts} ... {end}</code></b><br>"
        "Loops over every selected document. Inside this block, use <b><code>{context}</code></b> to output the document's content.<br><br>"
        "<b><code>{between}</code></b><br>"
        "Placed inside a <code>{foreach}</code> block to separate documents. Text after <code>{between}</code> is only inserted between documents, not at the very end.<br><br>"
        "<b>Example:</b><br>"
        "<code>{foreach contexts}</code><br>"
        "<code>Doc: {context}</code><br>"
        "<code>{between}</code><br>"
        "<code>---</code><br>"
        "<code>{end}</code><br><br>"
        "<b>Dynamic Inputs:</b><br>"
        "You can also use <code>{input \"Label\"}</code> or <code>{textarea \"Label\"}</code> to create custom input fields that will be requested before merging."
    );
    QMessageBox::information(this, tr("Template Help"), helpText);
}

void MergeDocumentsDialog::onTemplateChanged(int index) {
    QString templateName;
    if (index == 0) {
        m_instructionEdit->setPlainText(tr("Merge the following documents into one coherent document:\n\n{foreach contexts}\nDocument:\n{context}\n{between}\n---\n{end}"));
        templateName = tr("Default Merge");
    } else if (index > 0 && index - 1 < m_templates.size()) {
        m_instructionEdit->setPlainText(m_templates[index - 1].prompt);
        templateName = m_templates[index - 1].name;
    }

    if (!m_titleManuallyEdited) {
        QString titleStr = templateName;
        if (!m_defaultTitleSuffix.isEmpty()) {
            titleStr += ": " + m_defaultTitleSuffix;
        }
        if (titleStr.length() > 50) titleStr = titleStr.left(47) + "...";
        m_titleEdit->setText(titleStr);
    }
}

QString MergeDocumentsDialog::getFinalPrompt() const {
    return m_finalPrompt;
}

QStringList MergeDocumentsDialog::getSelectedModels() const {
    return m_selectedModels;
}

QString MergeDocumentsDialog::getRawPrompt() const {
    return m_instructionEdit->toPlainText();
}

void MergeDocumentsDialog::setInitialPrompt(const QString& prompt) {
    if (!prompt.isEmpty()) {
        m_instructionEdit->setPlainText(prompt);
    }
}

void MergeDocumentsDialog::setInitialModels(const QStringList& models) {
    if (!models.isEmpty()) {
        m_selectedModels = models;
        if (m_selectedModels.size() == 1) {
            m_selectModelsBtn->setText(m_selectedModels.first());
        } else {
            m_selectModelsBtn->setText(tr("%1 Models Selected").arg(m_selectedModels.size()));
        }
    }
}

void MergeDocumentsDialog::setIsRegenerating(bool isRegenerating) {
    m_isRegenerating = isRegenerating;
    m_actionTypeCombo->clear();

    if (m_isRegenerating) {
        m_actionTypeCombo->addItem(tr("Replace Merged Document (Add previous to version history)"), ReplaceExisting);
    }

    m_actionTypeCombo->addItem(tr("New Merged Document"), NewDocument);

    if (m_documentTitles.size() == 2) {
        m_actionTypeCombo->addItem(tr("Replace '%1' (Add to version history)").arg(m_documentTitles[0]), ReplaceDocA);
        m_actionTypeCombo->addItem(tr("Replace '%1' (Add to version history)").arg(m_documentTitles[1]), ReplaceDocB);
        m_actionTypeCombo->addItem(tr("Replace '%1' & '%2' (Delete both)").arg(m_documentTitles[0], m_documentTitles[1]), ReplaceDocAandB);
    }

    // Select the default action depending on regenerating status
    if (m_isRegenerating) {
        m_actionTypeCombo->setCurrentIndex(0); // ReplaceExisting
    } else {
        // Find NewDocument in the combo box
        int index = m_actionTypeCombo->findData(NewDocument);
        if (index != -1) {
            m_actionTypeCombo->setCurrentIndex(index);
        }
    }
}

MergeDocumentsDialog::MergeAction MergeDocumentsDialog::getSelectedAction() const {
    return static_cast<MergeAction>(m_actionTypeCombo->currentData().toInt());
}

QString MergeDocumentsDialog::getTitle() const {
    return m_titleEdit->text();
}

void MergeDocumentsDialog::setTitle(const QString& title) {
    m_titleEdit->setText(title);
    m_titleManuallyEdited = true;
}

void MergeDocumentsDialog::setDefaultTitleSuffix(const QString& suffix) {
    m_defaultTitleSuffix = suffix;
    if (!m_titleManuallyEdited) {
        onTemplateChanged(m_templateCombo->currentIndex());
    }
}

QString MergeDocumentsDialog::buildPreviewPrompt() const {
    QString prompt = m_instructionEdit->toPlainText();

    // 1. Process standard dynamic inputs {input} {textarea}
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
            const AiDynamicInputInfo& dinfo = m_currentDynamicInputs[j];

            QString val;
            if (QLineEdit* le = qobject_cast<QLineEdit*>(dinfo.widget)) {
                val = le->text();
            } else if (QTextEdit* te = qobject_cast<QTextEdit*>(dinfo.widget)) {
                val = te->toPlainText();
            }
            prompt.replace(minfo.start, minfo.length, val);
        }
    }

    return TemplateParser::parseMergeTemplate(prompt, m_documentContents);
}

void MergeDocumentsDialog::onPreviewClicked() {
    QString prompt = buildPreviewPrompt();
    QMessageBox::information(this, tr("Prompt Preview"), prompt);
}

void MergeDocumentsDialog::onSaveTemplateClicked() {
    bool ok;
    QString name = QInputDialog::getText(this, tr("Save Template"), tr("Template Name:"), QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Save Location"), tr("Save this template globally so it is available in all books?"), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (reply == QMessageBox::Cancel) return;

    AIOperation op;
    op.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    op.name = name;
    op.prompt = getRawPrompt();
    op.source = (reply == QMessageBox::Yes) ? "global" : "database";
    op.inputType = "multiple";

    if (reply == QMessageBox::Yes) {
        QList<AIOperation> ops = AIOperationsManager::getGlobalOperations();
        ops.append(op);
        AIOperationsManager::setGlobalOperations(ops);
    } else {
        QList<AIOperation> ops = AIOperationsManager::getDatabaseOperations(m_db);
        ops.append(op);
        AIOperationsManager::setDatabaseOperations(m_db, ops);
    }

    // Refresh combo
    loadTemplates();

    // Select the new template
    for (int j = 0; j < m_templateCombo->count(); ++j) {
        if (m_templateCombo->itemData(j).toString() == op.id) {
            m_templateCombo->setCurrentIndex(j);
            break;
        }
    }
}

void MergeDocumentsDialog::onSelectModelsClicked() {
    ModelSelectionDialog dlg(m_modelInfos, m_fallbackModels, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_selectedModels = dlg.selectedModels();
        if (m_selectedModels.isEmpty()) {
            m_selectModelsBtn->setText(tr("Select Model(s)"));
        } else if (m_selectedModels.size() == 1) {
            m_selectModelsBtn->setText(m_selectedModels.first());
        } else {
            m_selectModelsBtn->setText(tr("%1 Models Selected").arg(m_selectedModels.size()));
        }
    }
}

void MergeDocumentsDialog::updateDynamicInputs() {
    QString prompt = m_instructionEdit->toPlainText();

    QList<AiDynamicInputInfo> newInputs;

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
            AiDynamicInputInfo& info = newInputs[j];
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

void MergeDocumentsDialog::accept() {
    m_finalPrompt = buildPreviewPrompt();
    QDialog::accept();
}
