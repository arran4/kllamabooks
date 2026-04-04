#include "AiActionDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QRegularExpression>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>

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
    m_instructionEdit->setPlaceholderText("Configure prompt using {context}, {input \"label\"}, or {textarea \"label\"} placeholders...");
    m_instructionEdit->setAcceptRichText(false);
    m_instructionEdit->setPlainText(defaultTemplate);
    layout->addWidget(m_instructionEdit);

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
}

QString AiActionDialog::getPrompt() const {
    return m_finalPrompt;
}

struct InputWidgetInfo {
    int start;
    int length;
    QWidget* widget;
};

void AiActionDialog::accept() {
    QString prompt = m_instructionEdit->toPlainText();
    m_finalPrompt = prompt;

    QRegularExpression re("\\{(input|textarea)(?:\\s+\"([^\"]+)\")?\\}");
    QRegularExpressionMatchIterator i = re.globalMatch(prompt);

    if (i.hasNext()) {
        QDialog inputDlg(this);
        inputDlg.setWindowTitle(tr("Provide Inputs"));
        inputDlg.resize(400, 300);
        QVBoxLayout* layout = new QVBoxLayout(&inputDlg);
        QFormLayout* form = new QFormLayout();
        layout->addLayout(form);

        QList<InputWidgetInfo> inputWidgets;

        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString type = match.captured(1);
            QString label = match.captured(2);

            if (label.isEmpty()) {
                int start = match.capturedStart(0);
                int lineStart = prompt.lastIndexOf('\n', start - 1) + 1;
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

            QWidget* w = nullptr;
            if (type == "input") {
                w = new QLineEdit(&inputDlg);
            } else if (type == "textarea") {
                w = new QTextEdit(&inputDlg);
            }

            if (w) {
                form->addRow(label + ":", w);
                inputWidgets.append({match.capturedStart(0), match.capturedLength(0), w});
            }
        }

        QHBoxLayout* btnLayout = new QHBoxLayout();
        QPushButton* cancelBtn = new QPushButton(tr("Cancel"), &inputDlg);
        QPushButton* okBtn = new QPushButton(tr("OK"), &inputDlg);
        btnLayout->addStretch();
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(okBtn);
        layout->addLayout(btnLayout);

        connect(cancelBtn, &QPushButton::clicked, &inputDlg, &QDialog::reject);
        connect(okBtn, &QPushButton::clicked, &inputDlg, &QDialog::accept);

        if (inputDlg.exec() == QDialog::Accepted) {
            // Replace backwards to preserve indices
            for (int j = inputWidgets.size() - 1; j >= 0; --j) {
                const auto& info = inputWidgets[j];
                QString val;
                if (QLineEdit* le = qobject_cast<QLineEdit*>(info.widget)) {
                    val = le->text();
                } else if (QTextEdit* te = qobject_cast<QTextEdit*>(info.widget)) {
                    val = te->toPlainText();
                }
                prompt.replace(info.start, info.length, val);
            }
            prompt.replace("{context}", m_contextText);
            m_finalPrompt = prompt;
            QDialog::accept();
        }
    } else {
        prompt.replace("{context}", m_contextText);
        m_finalPrompt = prompt;
        QDialog::accept();
    }
}
