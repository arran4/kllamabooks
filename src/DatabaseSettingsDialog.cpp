#include "DatabaseSettingsDialog.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

DatabaseSettingsDialog::DatabaseSettingsDialog(BookDatabase* db, const QStringList& availableModels, QWidget* parent)
    : QDialog(parent), m_db(db) {
    setWindowTitle(tr("Database Settings"));
    resize(500, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QTabWidget* tabWidget = new QTabWidget(this);

    // General Tab
    QWidget* generalTab = new QWidget(this);
    QVBoxLayout* generalLayout = new QVBoxLayout(generalTab);
    QFormLayout* formLayout = new QFormLayout();

    m_defaultModelCombo = new QComboBox(this);
    m_defaultModelCombo->addItem("Use Application Default");
    m_defaultModelCombo->addItems(availableModels);
    QString currentModel = m_db->getSetting("book", 0, "defaultModel", "");
    if (!currentModel.isEmpty()) {
        int idx = m_defaultModelCombo->findText(currentModel);
        if (idx >= 0) m_defaultModelCombo->setCurrentIndex(idx);
    } else {
        m_defaultModelCombo->setCurrentIndex(0);
    }
    formLayout->addRow(tr("Default Model:"), m_defaultModelCombo);

    m_userNameEdit = new QLineEdit(this);
    m_userNameEdit->setText(m_db->getSetting("book", 0, "userName", "User"));
    formLayout->addRow(tr("User Name:"), m_userNameEdit);

    m_assistantNameEdit = new QLineEdit(this);
    m_assistantNameEdit->setText(m_db->getSetting("book", 0, "assistantName", "Assistant"));
    formLayout->addRow(tr("Assistant Name:"), m_assistantNameEdit);

    m_systemPromptEdit = new QTextEdit(this);
    m_systemPromptEdit->setAcceptRichText(false);
    m_systemPromptEdit->setPlainText(m_db->getSetting("book", 0, "systemPrompt", ""));
    formLayout->addRow(tr("System Prompt:"), m_systemPromptEdit);

    generalLayout->addLayout(formLayout);
    generalLayout->addStretch();

    tabWidget->addTab(generalTab, tr("General Settings"));

    // AI Operations Tab
    m_aiOperationsEditor = new AIOperationsEditorWidget("database", this);
    QList<AIOperation> inheritedOps = AIOperationsManager::getBuiltInOperations();
    inheritedOps.append(AIOperationsManager::getGlobalOperations());

    m_aiOperationsEditor->setOperations(AIOperationsManager::getDatabaseOperations(m_db), inheritedOps);
    tabWidget->addTab(m_aiOperationsEditor, tr("AI Operations"));

    mainLayout->addWidget(tabWidget);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
    QPushButton* saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setDefault(true);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    mainLayout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &DatabaseSettingsDialog::saveSettings);
}

void DatabaseSettingsDialog::saveSettings() {
    if (!m_db) return;

    QString model = m_defaultModelCombo->currentText();
    if (model == "Use Application Default") {
        model = "";
    }
    m_db->setSetting("book", 0, "defaultModel", model);
    m_db->setSetting("book", 0, "userName", m_userNameEdit->text().trimmed());
    m_db->setSetting("book", 0, "assistantName", m_assistantNameEdit->text().trimmed());
    m_db->setSetting("book", 0, "systemPrompt", m_systemPromptEdit->toPlainText().trimmed());

    AIOperationsManager::setDatabaseOperations(m_db, m_aiOperationsEditor->getOperations());

    accept();
}
