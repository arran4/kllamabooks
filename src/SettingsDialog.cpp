#include "SettingsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSpinBox>
#include <QTabWidget>

ConnectionDialog::ConnectionDialog(QWidget* parent, const QString& name, const QString& backend, const QString& url,
                                   const QString& authKey, int maxConcurrent)
    : QDialog(parent) {
    setWindowTitle(tr("Connection Settings"));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* formLayout = new QFormLayout();

    m_nameEdit = new QLineEdit(name, this);
    formLayout->addRow(tr("Name:"), m_nameEdit);

    m_backendCombo = new QComboBox(this);
    m_backendCombo->addItem("Ollama");
    m_backendCombo->setCurrentText(backend);
    formLayout->addRow(tr("Backend:"), m_backendCombo);

    m_urlEdit = new QLineEdit(url, this);
    formLayout->addRow(tr("URL:"), m_urlEdit);

    m_authKeyEdit = new QLineEdit(authKey, this);
    m_authKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    formLayout->addRow(tr("Auth Key:"), m_authKeyEdit);

    m_maxConcurrentSpinBox = new QSpinBox(this);
    m_maxConcurrentSpinBox->setRange(1, 100);
    m_maxConcurrentSpinBox->setValue(maxConcurrent);
    formLayout->addRow(tr("Max Concurrent:"), m_maxConcurrentSpinBox);

    mainLayout->addLayout(formLayout);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_testButton = new QPushButton(tr("Test"), this);
    QPushButton* okButton = new QPushButton(tr("OK"), this);
    QPushButton* cancelButton = new QPushButton(tr("Cancel"), this);

    btnLayout->addWidget(m_testButton);
    btnLayout->addStretch();
    btnLayout->addWidget(okButton);
    btnLayout->addWidget(cancelButton);

    mainLayout->addLayout(btnLayout);

    connect(m_testButton, &QPushButton::clicked, this, &ConnectionDialog::onTestConnection);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

ConnectionDialog::~ConnectionDialog() {}

QString ConnectionDialog::name() const { return m_nameEdit->text(); }

QString ConnectionDialog::backend() const { return m_backendCombo->currentText(); }

QString ConnectionDialog::url() const { return m_urlEdit->text(); }

QString ConnectionDialog::authKey() const { return m_authKeyEdit->text(); }

int ConnectionDialog::maxConcurrent() const { return m_maxConcurrentSpinBox->value(); }

void ConnectionDialog::onTestConnection() {
    m_testButton->setEnabled(false);

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QString urlStr = m_urlEdit->text();
    QString authKey = m_authKeyEdit->text();
    if (!urlStr.endsWith("/")) urlStr += "/";
    urlStr += "api/tags";

    QNetworkRequest request((QUrl(urlStr)));
    if (!authKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + authKey).toUtf8());
    }
    QNetworkReply* reply = manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        m_testButton->setEnabled(true);
        if (reply->error() == QNetworkReply::NoError) {
            QMessageBox::information(this, tr("Connection Test"), tr("Connection successful!"));
        } else {
            QMessageBox::warning(this, tr("Connection Test"), tr("Connection failed: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
        manager->deleteLater();
    });
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    resize(600, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QTabWidget* tabWidget = new QTabWidget(this);

    // General Settings Tab
    QWidget* generalTab = new QWidget(this);
    QVBoxLayout* generalLayout = new QVBoxLayout(generalTab);

    // Chat Settings Section
    QLabel* chatSettingsLabel = new QLabel(tr("<b>Chat Settings</b>"), this);
    generalLayout->addWidget(chatSettingsLabel);

    QHBoxLayout* fontLayout = new QHBoxLayout();
    QLabel* fontLabel = new QLabel(tr("Global Editor Font:"), this);
    m_fontFamilyCombo = new QFontComboBox(this);
    QLabel* sizeLabel = new QLabel(tr("Size:"), this);
    m_fontSizeSpinBox = new QSpinBox(this);
    m_fontSizeSpinBox->setRange(6, 72);
    fontLayout->addWidget(fontLabel);
    fontLayout->addWidget(m_fontFamilyCombo);
    fontLayout->addWidget(sizeLabel);
    fontLayout->addWidget(m_fontSizeSpinBox);
    fontLayout->addStretch();
    generalLayout->addLayout(fontLayout);

    generalLayout->addSpacing(10);

    QHBoxLayout* sendBehaviorLayout = new QHBoxLayout();
    QLabel* sendBehaviorLabel = new QLabel(tr("Global Send Behavior:"), this);
    m_sendBehaviorCombo = new QComboBox(this);
    m_sendBehaviorCombo->addItem(tr("Enter to Send, Shift+Enter for Newline"), "EnterToSend");
    m_sendBehaviorCombo->addItem(tr("Ctrl+Enter to Send, Enter for Newline"), "CtrlEnterToSend");
    sendBehaviorLayout->addWidget(sendBehaviorLabel);
    sendBehaviorLayout->addWidget(m_sendBehaviorCombo);
    sendBehaviorLayout->addStretch();
    generalLayout->addLayout(sendBehaviorLayout);

    QHBoxLayout* sysPromptLayout = new QHBoxLayout();
    QLabel* sysPromptLabel = new QLabel(tr("Global System Prompt:"), this);
    m_globalSystemPromptEdit = new QTextEdit(this);
    m_globalSystemPromptEdit->setAcceptRichText(false);
    m_globalSystemPromptEdit->setPlainText(m_settings.value("globalSystemPrompt", "").toString());
    sysPromptLayout->addWidget(sysPromptLabel);
    sysPromptLayout->addWidget(m_globalSystemPromptEdit);
    generalLayout->addLayout(sysPromptLayout);

    QHBoxLayout* queueLayout = new QHBoxLayout();
    QLabel* queueLabel = new QLabel(tr("Queue Processing:"), this);
    m_queueProcessingCombo = new QComboBox(this);
    m_queueProcessingCombo->addItem(tr("FCFS"), "FCFS");
    m_queueProcessingCombo->addItem(tr("LCFS"), "LCFS");
    m_queueProcessingCombo->addItem(tr("Smallest message first"), "Smallest message first");
    m_queueProcessingCombo->addItem(tr("Largest message first"), "Largest message first");
    m_queueProcessingCombo->addItem(tr("Smallest model first"), "Smallest model first");
    m_queueProcessingCombo->addItem(tr("Largest model first"), "Largest model first");
    queueLayout->addWidget(queueLabel);
    queueLayout->addWidget(m_queueProcessingCombo);
    queueLayout->addStretch();

    m_prioritizeSameModelCheck = new QCheckBox(tr("Prioritize jobs with the same model as just ran"), this);
    queueLayout->addWidget(m_prioritizeSameModelCheck);

    generalLayout->addLayout(queueLayout);

    generalLayout->addSpacing(10);

    // LLM Section
    QLabel* llmLabel = new QLabel(tr("<b>LLM Connections</b>"), this);
    generalLayout->addWidget(llmLabel);

    m_connectionsTable = new QTableWidget(this);
    m_connectionsTable->setColumnCount(5);
    m_connectionsTable->setHorizontalHeaderLabels({tr("Name"), tr("Backend"), tr("URL"), tr("Auth Key"), tr("Max Concurrent")});
    m_connectionsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_connectionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_connectionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    generalLayout->addWidget(m_connectionsTable);

    QHBoxLayout* btnManageLayout = new QHBoxLayout();
    m_addButton = new QPushButton(tr("Add"), this);
    m_editButton = new QPushButton(tr("Edit"), this);
    m_removeButton = new QPushButton(tr("Remove"), this);
    m_testButton = new QPushButton(tr("Test Selected"), this);

    btnManageLayout->addWidget(m_addButton);
    btnManageLayout->addWidget(m_editButton);
    btnManageLayout->addWidget(m_removeButton);
    btnManageLayout->addStretch();
    btnManageLayout->addWidget(m_testButton);
    generalLayout->addLayout(btnManageLayout);

    generalLayout->addStretch();

    tabWidget->addTab(generalTab, tr("General Settings"));

    // AI Operations Tab
    m_aiOperationsEditor = new AIOperationsEditorWidget("global", this);
    m_aiOperationsEditor->setOperations(AIOperationsManager::getGlobalOperations(),
                                        AIOperationsManager::getBuiltInOperations());
    tabWidget->addTab(m_aiOperationsEditor, tr("AI Operations"));

    // Document Templates Tab
    m_documentTemplatesEditor = new DocumentTemplatesEditorWidget("global", this);
    m_documentTemplatesEditor->setTemplates(DocumentTemplatesManager::getGlobalTemplates(),
                                            DocumentTemplatesManager::getBuiltInTemplates());
    tabWidget->addTab(m_documentTemplatesEditor, tr("Document Templates"));

    mainLayout->addWidget(tabWidget);

    // Dialog buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_applyButton = new QPushButton(tr("Apply"), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    btnLayout->addStretch();
    btnLayout->addWidget(m_applyButton);
    btnLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(btnLayout);

    // Connections
    connect(m_addButton, &QPushButton::clicked, this, &SettingsDialog::onAddConnection);
    connect(m_editButton, &QPushButton::clicked, this, &SettingsDialog::onEditConnection);
    connect(m_removeButton, &QPushButton::clicked, this, &SettingsDialog::onRemoveConnection);
    connect(m_testButton, &QPushButton::clicked, this, &SettingsDialog::onTestConnection);
    connect(m_applyButton, &QPushButton::clicked, this, &SettingsDialog::onApply);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    loadConnections();

    QFont defaultFont = QApplication::font();
    m_fontFamilyCombo->setCurrentFont(QFont(m_settings.value("globalFontFamily", defaultFont.family()).toString()));
    m_fontSizeSpinBox->setValue(m_settings.value("globalFontSize", defaultFont.pointSize()).toInt());

    QString globalBehavior = m_settings.value("globalSendBehavior", "EnterToSend").toString();
    int index = m_sendBehaviorCombo->findData(globalBehavior);
    if (index >= 0) {
        m_sendBehaviorCombo->setCurrentIndex(index);
    }

    QString queueProc = m_settings.value("queueProcessing", "FCFS").toString();
    int queueIdx = m_queueProcessingCombo->findData(queueProc);
    if (queueIdx >= 0) {
        m_queueProcessingCombo->setCurrentIndex(queueIdx);
    }

    m_prioritizeSameModelCheck->setChecked(m_settings.value("prioritizeSameModel", false).toBool());
}

SettingsDialog::~SettingsDialog() {}

void SettingsDialog::loadConnections() {
    QVariantList connections = m_settings.value("llmConnections").toList();
    if (connections.isEmpty() && m_settings.contains("ollamaUrl")) {
        // Migration from old single URL setting
        QVariantMap defaultConn;
        defaultConn["name"] = "Default Ollama";
        defaultConn["backend"] = "Ollama";
        defaultConn["url"] = m_settings.value("ollamaUrl", "http://localhost:11434").toString();
        defaultConn["authKey"] = "";
        defaultConn["maxConcurrent"] = 1;
        connections.append(defaultConn);
    }

    m_connectionsTable->setRowCount(0);
    for (const QVariant& v : connections) {
        QVariantMap map = v.toMap();
        int row = m_connectionsTable->rowCount();
        m_connectionsTable->insertRow(row);
        m_connectionsTable->setItem(row, 0, new QTableWidgetItem(map["name"].toString()));
        m_connectionsTable->setItem(row, 1, new QTableWidgetItem(map.value("backend", "Ollama").toString()));
        m_connectionsTable->setItem(row, 2, new QTableWidgetItem(map["url"].toString()));
        m_connectionsTable->setItem(row, 3, new QTableWidgetItem(map["authKey"].toString()));
        m_connectionsTable->setItem(row, 4, new QTableWidgetItem(map.value("maxConcurrent", 1).toString()));
    }
}

void SettingsDialog::saveConnections() {
    QVariantList connections;
    for (int i = 0; i < m_connectionsTable->rowCount(); ++i) {
        QVariantMap map;
        map["name"] = m_connectionsTable->item(i, 0)->text();
        map["backend"] = m_connectionsTable->item(i, 1)->text();
        map["url"] = m_connectionsTable->item(i, 2)->text();
        map["authKey"] = m_connectionsTable->item(i, 3)->text();
        map["maxConcurrent"] = m_connectionsTable->item(i, 4)->text().toInt();
        connections.append(map);
    }
    m_settings.setValue("llmConnections", connections);
}

void SettingsDialog::onAddConnection() {
    ConnectionDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        int row = m_connectionsTable->rowCount();
        m_connectionsTable->insertRow(row);
        m_connectionsTable->setItem(row, 0, new QTableWidgetItem(dialog.name()));
        m_connectionsTable->setItem(row, 1, new QTableWidgetItem(dialog.backend()));
        m_connectionsTable->setItem(row, 2, new QTableWidgetItem(dialog.url()));
        m_connectionsTable->setItem(row, 3, new QTableWidgetItem(dialog.authKey()));
        m_connectionsTable->setItem(row, 4, new QTableWidgetItem(QString::number(dialog.maxConcurrent())));
    }
}

void SettingsDialog::onEditConnection() {
    int row = m_connectionsTable->currentRow();
    if (row < 0) return;

    QString name = m_connectionsTable->item(row, 0)->text();
    QString backend = m_connectionsTable->item(row, 1)->text();
    QString url = m_connectionsTable->item(row, 2)->text();
    QString authKey = m_connectionsTable->item(row, 3)->text();
    int maxConcurrent = m_connectionsTable->item(row, 4)->text().toInt();
    if (maxConcurrent < 1) maxConcurrent = 1;

    ConnectionDialog dialog(this, name, backend, url, authKey, maxConcurrent);
    if (dialog.exec() == QDialog::Accepted) {
        m_connectionsTable->item(row, 0)->setText(dialog.name());
        m_connectionsTable->item(row, 1)->setText(dialog.backend());
        m_connectionsTable->item(row, 2)->setText(dialog.url());
        m_connectionsTable->item(row, 3)->setText(dialog.authKey());
        m_connectionsTable->item(row, 4)->setText(QString::number(dialog.maxConcurrent()));
    }
}

void SettingsDialog::onRemoveConnection() {
    int row = m_connectionsTable->currentRow();
    if (row >= 0) {
        m_connectionsTable->removeRow(row);
    }
}

void SettingsDialog::onTestConnection() {
    int row = m_connectionsTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, tr("Test Connection"), tr("Please select a connection to test."));
        return;
    }

    m_testButton->setEnabled(false);

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QString urlStr = m_connectionsTable->item(row, 2)->text();
    QString authKey = m_connectionsTable->item(row, 3)->text();
    if (!urlStr.endsWith("/")) urlStr += "/";
    urlStr += "api/tags";

    QNetworkRequest request((QUrl(urlStr)));
    if (!authKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + authKey).toUtf8());
    }
    QNetworkReply* reply = manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, manager]() {
        m_testButton->setEnabled(true);
        if (reply->error() == QNetworkReply::NoError) {
            QMessageBox::information(this, tr("Connection Test"), tr("Connection successful!"));
        } else {
            QMessageBox::warning(this, tr("Connection Test"), tr("Connection failed: %1").arg(reply->errorString()));
        }
        reply->deleteLater();
        manager->deleteLater();
    });
}

void SettingsDialog::onApply() {
    saveConnections();

    QString selectedBehavior = m_sendBehaviorCombo->currentData().toString();

    m_settings.setValue("globalFontFamily", m_fontFamilyCombo->currentFont().family());
    m_settings.setValue("globalFontSize", m_fontSizeSpinBox->value());

    m_settings.setValue("globalSendBehavior", selectedBehavior);
    m_settings.setValue("globalSystemPrompt", m_globalSystemPromptEdit->toPlainText().trimmed());
    m_settings.setValue("queueProcessing", m_queueProcessingCombo->currentData().toString());
    m_settings.setValue("prioritizeSameModel", m_prioritizeSameModelCheck->isChecked());

    AIOperationsManager::setGlobalOperations(m_aiOperationsEditor->getOperations());
    DocumentTemplatesManager::setGlobalTemplates(m_documentTemplatesEditor->getTemplates());

    emit settingsApplied();
    accept();
}
