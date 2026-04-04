#include "AIOperationsDialog.h"
#include "AIOperationsManager.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>

AIOperationsDialog::AIOperationsDialog(BookDatabase* db, const QString& defaultPrompt, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("AI Document Operations"));
    resize(500, 400);

    QVBoxLayout* layout = new QVBoxLayout(this);

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
    layout->addLayout(opLayout);

    // Prompt
    layout->addWidget(new QLabel(tr("Prompt Instructions:")));
    m_promptEdit = new QTextEdit(this);
    m_promptEdit->setPlainText(defaultPrompt);
    layout->addWidget(m_promptEdit);

    // Dynamic Inputs
    m_dynamicInputsLayout = new QFormLayout();
    layout->addLayout(m_dynamicInputsLayout);

    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    QPushButton* generateBtn = new QPushButton(tr("Generate"), this);
    generateBtn->setDefault(true);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(generateBtn);
    layout->addLayout(btnLayout);

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
