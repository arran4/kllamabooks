#include "ChatSettingsDialog.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ChatSettingsDialog::ChatSettingsDialog(const QString& initialTitle, const QString& initialSystemPrompt,
                                       const QString& initialSendBehavior, const QString& initialModel,
                                       const QString& initialMultiLine, const QString& initialDraftPrompt,
                                       const QString& initialUserNote, const QString& endpointName,
                                       const QStringList& availableModels, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Chat Settings"));
    setMinimumWidth(400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setText(initialTitle);
    m_titleEdit->setPlaceholderText(tr("Inherit / Default"));
    formLayout->addRow(tr("Title:"), m_titleEdit);

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

    m_modelCombo = new QComboBox(this);
    m_modelCombo->addItem(tr("Use Parent Default"), "default");
    for (const QString& model : availableModels) {
        QString displayString = QString("%1's %2").arg(endpointName, model);
        m_modelCombo->addItem(displayString, model);
    }
    int modelIndex = m_modelCombo->findData(initialModel);
    if (modelIndex >= 0) {
        m_modelCombo->setCurrentIndex(modelIndex);
    } else {
        m_modelCombo->setCurrentIndex(0);
    }
    formLayout->addRow(tr("Model Selection:"), m_modelCombo);

    m_multiLineCombo = new QComboBox(this);
    m_multiLineCombo->addItem(tr("Use Parent Default"), "default");
    m_multiLineCombo->addItem(tr("Single Line"), "Single Line");
    m_multiLineCombo->addItem(tr("Multi Line"), "Multi Line");
    int mlIndex = m_multiLineCombo->findData(initialMultiLine);
    if (mlIndex >= 0) {
        m_multiLineCombo->setCurrentIndex(mlIndex);
    } else {
        m_multiLineCombo->setCurrentIndex(0);
    }
    formLayout->addRow(tr("Input Mode:"), m_multiLineCombo);

    m_draftPromptEdit = new QTextEdit(this);
    m_draftPromptEdit->setAcceptRichText(false);
    m_draftPromptEdit->setPlainText(initialDraftPrompt);
    formLayout->addRow(tr("Incomplete Prompt (Draft):"), m_draftPromptEdit);

    m_userNoteEdit = new QTextEdit(this);
    m_userNoteEdit->setAcceptRichText(false);
    m_userNoteEdit->setPlainText(initialUserNote);
    formLayout->addRow(tr("Comments / Notes:"), m_userNoteEdit);

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

QString ChatSettingsDialog::getTitle() const { return m_titleEdit->text().trimmed(); }

QString ChatSettingsDialog::getSystemPrompt() const { return m_systemPromptEdit->toPlainText().trimmed(); }

QString ChatSettingsDialog::getSendBehavior() const { return m_sendBehaviorCombo->currentData().toString(); }

QString ChatSettingsDialog::getModel() const { return m_modelCombo->currentData().toString(); }

QString ChatSettingsDialog::getMultiLine() const { return m_multiLineCombo->currentData().toString(); }

QString ChatSettingsDialog::getDraftPrompt() const { return m_draftPromptEdit->toPlainText(); }

QString ChatSettingsDialog::getUserNote() const { return m_userNoteEdit->toPlainText(); }
