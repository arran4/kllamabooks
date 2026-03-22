#include "ChatSettingsDialog.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ChatSettingsDialog::ChatSettingsDialog(const QString& initialSystemPrompt, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Chat Settings"));
    setMinimumWidth(400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_systemPromptEdit = new QTextEdit(this);
    m_systemPromptEdit->setAcceptRichText(false);
    m_systemPromptEdit->setPlainText(initialSystemPrompt);
    formLayout->addRow(tr("System Prompt:"), m_systemPromptEdit);

    mainLayout->addLayout(formLayout);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    QPushButton* saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setDefault(true);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    mainLayout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &QDialog::accept);
}

QString ChatSettingsDialog::getSystemPrompt() const {
    return m_systemPromptEdit->toPlainText().trimmed();
}
