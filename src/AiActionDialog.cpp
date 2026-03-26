#include "AiActionDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
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
    m_instructionEdit->setPlaceholderText("Configure prompt using {context} placeholder...");
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
    QString prompt = m_instructionEdit->toPlainText();
    return prompt.replace("{context}", m_contextText);
}
