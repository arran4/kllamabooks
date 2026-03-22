#include "ChatSettingsDialog.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ChatSettingsDialog::ChatSettingsDialog(const QString& initialSystemPrompt, const QString& initialSendBehavior, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Chat Settings"));
    setMinimumWidth(400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_systemPromptEdit = new QTextEdit(this);
    m_systemPromptEdit->setAcceptRichText(false);
    m_systemPromptEdit->setPlainText(initialSystemPrompt);
    formLayout->addRow(tr("System Prompt:"), m_systemPromptEdit);

    m_sendBehaviorCombo = new QComboBox(this);
    m_sendBehaviorCombo->addItem(tr("Use Parent Default"), "default");
    m_sendBehaviorCombo->addItem(tr("Enter to Send, Shift+Enter for Newline"), "EnterToSend");
    m_sendBehaviorCombo->addItem(tr("Ctrl+Enter to Send, Enter for Newline"), "CtrlEnterToSend");
    int behaviorIndex = m_sendBehaviorCombo->findData(initialSendBehavior);
    if (behaviorIndex >= 0) {
        m_sendBehaviorCombo->setCurrentIndex(behaviorIndex);
    } else {
        m_sendBehaviorCombo->setCurrentIndex(0);
    }
    formLayout->addRow(tr("Send Behavior:"), m_sendBehaviorCombo);

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

QString ChatSettingsDialog::getSendBehavior() const {
    return m_sendBehaviorCombo->currentData().toString();
}
