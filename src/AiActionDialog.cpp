#include "AiActionDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

AiActionDialog::AiActionDialog(const QString& title, const QString& labelText, const QString& defaultTemplate,
                               const QString& contextText, QWidget* parent)
    : QDialog(parent), m_contextText(contextText) {
    setWindowTitle(title);
    setMinimumWidth(500);
    setMinimumHeight(400);

    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* instructionLabel = new QLabel(labelText, this);
    layout->addWidget(instructionLabel);

    m_instructionEdit = new QTextEdit(this);
    m_instructionEdit->setPlaceholderText(
        "Configure prompt using {context}, {input \"label\"}, or {textarea \"label\"} placeholders...");
    m_instructionEdit->setAcceptRichText(false);
    m_instructionEdit->setPlainText(defaultTemplate);
    layout->addWidget(m_instructionEdit);

    // Dynamic Inputs
    m_dynamicInputsLayout = new QFormLayout();
    layout->addLayout(m_dynamicInputsLayout);

    if (!contextText.isEmpty()) {
        QLabel* contextLabel = new QLabel("Selected Text Context:", this);
        layout->addWidget(contextLabel);

        QTextEdit* contextEdit = new QTextEdit(this);
        contextEdit->setReadOnly(true);
        contextEdit->setPlainText(contextText);
        contextEdit->setMaximumHeight(100);
        layout->addWidget(contextEdit);
    }

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(m_instructionEdit, &QTextEdit::textChanged, this, &AiActionDialog::updateDynamicInputs);
    updateDynamicInputs();
}

QString AiActionDialog::getPrompt() const { return m_finalPrompt; }

void AiActionDialog::updateDynamicInputs() {
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

void AiActionDialog::accept() {
    QString prompt = m_instructionEdit->toPlainText();

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

    prompt.replace("{context}", m_contextText);
    m_finalPrompt = prompt;
    QDialog::accept();
}
