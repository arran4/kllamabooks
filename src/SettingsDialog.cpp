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
#include <QUuid>

#include "AppCredentialManager.h"
#include "ConnectionMigration.h"
#include "CredentialStore.h"

ConnectionDialog::ConnectionDialog(QWidget* parent, const QString& name, const QString& backend, const QString& url,
                                   const QString& authKey, int maxConcurrent, bool hasCredential, const QString& id)
    : QDialog(parent), m_hasCredential(hasCredential), m_id(id), m_authKeyEdited(false) {
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
    if (m_hasCredential) {
        m_authKeyEdit->setPlaceholderText(tr("<Secret hidden>"));
    }
    connect(m_authKeyEdit, &QLineEdit::textEdited, this, [this]() { m_authKeyEdited = true; });
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

bool ConnectionDialog::isAuthKeyEdited() const { return m_authKeyEdited; }

int ConnectionDialog::maxConcurrent() const { return m_maxConcurrentSpinBox->value(); }

void ConnectionDialog::onTestConnection() {
    m_testButton->setEnabled(false);

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QString urlStr = m_urlEdit->text();

    QString authKeyToUse;
    if (m_authKeyEdited || !m_hasCredential) {
        authKeyToUse = m_authKeyEdit->text();
    } else {
        CredentialStore::Result res = AppCredentialManager::getCredential(m_id, authKeyToUse);
        if (res != CredentialStore::Result::Success) {
            QMessageBox::warning(this, tr("Test Connection Failed"),
                                 tr("Failed to read the credential from the secure wallet."));
            m_testButton->setEnabled(true);
            manager->deleteLater();
            return;
        }
    }
    QString authKey = authKeyToUse;
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
    m_connectionsTable->setHorizontalHeaderLabels(
        {tr("Name"), tr("Backend"), tr("URL"), tr("Auth Key"), tr("Max Concurrent")});
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
    bool needsSave = false;

    QScopedPointer<CredentialStore> defaultStore;
    CredentialStore* credentialStore = AppCredentialManager::getStoreFactory()();
    if (!credentialStore) {
        defaultStore.reset(new KWalletCredentialStore());
        credentialStore = defaultStore.data();
    }

    QStringList migrationErrors;

    if (connections.isEmpty() && m_settings.contains("ollamaUrl")) {
        // Migration from old single URL setting
        QVariantMap defaultConn;
        defaultConn["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        defaultConn["name"] = "Default Ollama";
        defaultConn["backend"] = "Ollama";
        defaultConn["url"] = m_settings.value("ollamaUrl", "http://localhost:11434").toString();
        defaultConn["maxConcurrent"] = 1;
        defaultConn["hasCredential"] = false;
        connections.append(defaultConn);
        needsSave = true;
    }

    bool migrated = ConnectionMigration::migrate(connections, *credentialStore, migrationErrors);
    if (migrated || needsSave) {
        m_settings.setValue("llmConnections", connections);
    }

    if (!migrationErrors.isEmpty()) {
        QMessageBox::warning(this, tr("Credential Migration Failed"),
                             tr("Some connections failed to migrate their credentials securely:\n\n%1\n\nThey will "
                                "continue to use insecure storage until migration succeeds.")
                                 .arg(migrationErrors.join("\n")));
    }

    m_connectionsTable->setRowCount(0);
    for (const QVariant& v : connections) {
        QVariantMap map = v.toMap();
        int row = m_connectionsTable->rowCount();
        m_connectionsTable->insertRow(row);
        m_connectionsTable->setItem(row, 0, new QTableWidgetItem(map["name"].toString()));
        m_connectionsTable->setItem(row, 1, new QTableWidgetItem(map.value("backend", "Ollama").toString()));
        m_connectionsTable->setItem(row, 2, new QTableWidgetItem(map["url"].toString()));

        bool hasCred = map.value("hasCredential", false).toBool();
        QTableWidgetItem* credItem = new QTableWidgetItem(hasCred ? tr("Configured") : tr("Not configured"));
        credItem->setData(Qt::UserRole, map["id"].toString());
        credItem->setData(Qt::UserRole + 1, hasCred);
        if (!hasCred && map.contains("authKey")) {
            m_legacyCredentials[map["id"].toString()] = map["authKey"].toString();
        }
        m_connectionsTable->setItem(row, 3, credItem);

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

        QTableWidgetItem* credItem = m_connectionsTable->item(i, 3);
        QString id = credItem->data(Qt::UserRole).toString();
        map["id"] = id;
        map["hasCredential"] = credItem->data(Qt::UserRole + 1).toBool();

        if (m_legacyCredentials.contains(id)) {
            map["authKey"] = m_legacyCredentials[id];
        }

        // IMPORTANT: We never write a newly entered password into QSettings.
        // If m_pendingWrites contains it, it means the wallet write failed during onApply.
        // We do not save it as plaintext.

        map["maxConcurrent"] = m_connectionsTable->item(i, 4)->text().toInt();
        connections.append(map);
    }
    m_settings.setValue("llmConnections", connections);
}

void SettingsDialog::onAddConnection() {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ConnectionDialog dialog(this, "New Connection", "Ollama", "http://localhost:11434", "", 1, false, id);
    if (dialog.exec() == QDialog::Accepted) {
        bool hasCred = false;
        if (!dialog.authKey().isEmpty()) {
            m_pendingWrites[id] = dialog.authKey();
            hasCred = true;  // Assume true until Apply
            m_pendingDeletes.remove(id);
        }

        int row = m_connectionsTable->rowCount();
        m_connectionsTable->insertRow(row);
        m_connectionsTable->setItem(row, 0, new QTableWidgetItem(dialog.name()));
        m_connectionsTable->setItem(row, 1, new QTableWidgetItem(dialog.backend()));
        m_connectionsTable->setItem(row, 2, new QTableWidgetItem(dialog.url()));

        QTableWidgetItem* credItem = new QTableWidgetItem(hasCred ? tr("Configured") : tr("Not configured"));
        credItem->setData(Qt::UserRole, id);
        credItem->setData(Qt::UserRole + 1, hasCred);
        m_connectionsTable->setItem(row, 3, credItem);

        m_connectionsTable->setItem(row, 4, new QTableWidgetItem(QString::number(dialog.maxConcurrent())));
    }
}

void SettingsDialog::onEditConnection() {
    int row = m_connectionsTable->currentRow();
    if (row < 0) return;

    QString name = m_connectionsTable->item(row, 0)->text();
    QString backend = m_connectionsTable->item(row, 1)->text();
    QString url = m_connectionsTable->item(row, 2)->text();

    QTableWidgetItem* credItem = m_connectionsTable->item(row, 3);
    QString id = credItem->data(Qt::UserRole).toString();
    bool hasCred = credItem->data(Qt::UserRole + 1).toBool();

    int maxConcurrent = m_connectionsTable->item(row, 4)->text().toInt();
    if (maxConcurrent < 1) maxConcurrent = 1;

    bool effectivelyHasCred = hasCred || m_legacyCredentials.contains(id);
    ConnectionDialog dialog(this, name, backend, url, "", maxConcurrent, effectivelyHasCred, id);
    if (dialog.exec() == QDialog::Accepted) {
        if (dialog.isAuthKeyEdited()) {
            if (dialog.authKey().isEmpty()) {
                m_pendingDeletes.insert(id);
                m_pendingWrites.remove(id);
                hasCred = false;
            } else {
                m_pendingWrites[id] = dialog.authKey();
                m_pendingDeletes.remove(id);
                hasCred = true;  // Assume true until Apply
            }
        }

        m_connectionsTable->item(row, 0)->setText(dialog.name());
        m_connectionsTable->item(row, 1)->setText(dialog.backend());
        m_connectionsTable->item(row, 2)->setText(dialog.url());

        credItem->setText(hasCred ? tr("Configured") : tr("Not configured"));
        credItem->setData(Qt::UserRole + 1, hasCred);

        m_connectionsTable->item(row, 4)->setText(QString::number(dialog.maxConcurrent()));
    }
}

void SettingsDialog::onRemoveConnection() {
    int row = m_connectionsTable->currentRow();
    if (row >= 0) {
        QTableWidgetItem* credItem = m_connectionsTable->item(row, 3);
        QString id = credItem->data(Qt::UserRole).toString();

        m_pendingDeletes.insert(id);
        m_pendingWrites.remove(id);

        // Hide it so we can unhide it on rollback failure
        m_connectionsTable->setRowHidden(row, true);
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

    QTableWidgetItem* credItem = m_connectionsTable->item(row, 3);
    QString id = credItem->data(Qt::UserRole).toString();
    bool hasCred = credItem->data(Qt::UserRole + 1).toBool();
    QString fallbackAuthKey = credItem->data(Qt::UserRole + 2).toString();

    QString authKey;
    if (m_pendingWrites.contains(id)) {
        authKey = m_pendingWrites[id];
    } else if (!m_pendingDeletes.contains(id)) {
        CredentialStore::Result res = AppCredentialManager::getCredential(id, authKey);
        if (hasCred && res != CredentialStore::Result::Success) {
            QMessageBox::warning(this, tr("Test Connection Failed"),
                                 tr("Failed to read the credential from the secure wallet."));
            m_testButton->setEnabled(true);
            manager->deleteLater();
            return;
        }
    }

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
    QScopedPointer<CredentialStore> defaultStore;
    CredentialStore* credentialStore = AppCredentialManager::getStoreFactory()();
    if (!credentialStore) {
        defaultStore.reset(new KWalletCredentialStore());
        credentialStore = defaultStore.data();
    }

    QStringList failedWrites;
    QStringList failedDeletes;

    // Sequence of operations to reverse successful steps if a later one fails
    QList<std::function<CredentialStore::Result()>> rollbackOperations;

    bool transactionFailed = false;

    // Apply pending deletes. Any failure (including WalletUnavailable) is a failed delete.
    for (const QString& id : m_pendingDeletes) {
        QString existingSecret;
        CredentialStore::Result readRes = credentialStore->readCredential(id, existingSecret);
        if (readRes != CredentialStore::Result::Success && readRes != CredentialStore::Result::NotFound) {
            // Cannot reliably rollback if we can't read the previous state
            failedDeletes.append(id);
            transactionFailed = true;
            break;
        }

        CredentialStore::Result res = credentialStore->deleteCredential(id);
        if (res != CredentialStore::Result::Success && res != CredentialStore::Result::NotFound) {
            failedDeletes.append(id);
            transactionFailed = true;
            break;  // Stop immediately
        }

        if (readRes == CredentialStore::Result::Success) {
            rollbackOperations.prepend([=]() { return credentialStore->writeCredential(id, existingSecret); });
        }
    }

    if (!transactionFailed) {
        // Apply pending writes. Any failure is a failed write.
        for (auto it = m_pendingWrites.begin(); it != m_pendingWrites.end(); ++it) {
            QString existingSecret;
            CredentialStore::Result readRes = credentialStore->readCredential(it.key(), existingSecret);
            if (readRes != CredentialStore::Result::Success && readRes != CredentialStore::Result::NotFound) {
                failedWrites.append(it.key());
                transactionFailed = true;
                break;
            }

            CredentialStore::Result res = credentialStore->writeCredential(it.key(), it.value());
            if (res != CredentialStore::Result::Success) {
                failedWrites.append(it.key());
                transactionFailed = true;
                break;  // Stop immediately
            }

            if (readRes == CredentialStore::Result::Success) {
                rollbackOperations.prepend(
                    [=]() { return credentialStore->writeCredential(it.key(), existingSecret); });
            } else {
                rollbackOperations.prepend([=]() { return credentialStore->deleteCredential(it.key()); });
            }
        }
    }

    if (transactionFailed) {
        // Rollback whatever we did successfully in reverse order
        bool rollbackFailed = false;
        for (const auto& rollbackOp : rollbackOperations) {
            if (rollbackOp() != CredentialStore::Result::Success) {
                rollbackFailed = true;
            }
        }

        if (rollbackFailed) {
            QMessageBox::critical(
                this, tr("Critical Failure"),
                tr("Failed to update credentials securely in KWallet, and automatic rollback also encountered errors. "
                   "Your credentials may be in an inconsistent state. Please check your wallet manually."));
        } else {
            QMessageBox::warning(this, tr("Save Failed"),
                                 tr("Failed to update credentials securely in KWallet. Your edits have not been saved. "
                                    "Please resolve wallet issues and try again."));
        }

        // Unhide deleted rows since we didn't apply
        for (int i = 0; i < m_connectionsTable->rowCount(); ++i) {
            m_connectionsTable->setRowHidden(i, false);
        }
        return;
    }

    // Success, we can now remove successfully written items from legacy credentials
    for (auto it = m_pendingWrites.begin(); it != m_pendingWrites.end(); ++it) {
        m_legacyCredentials.remove(it.key());
    }
    for (const QString& id : m_pendingDeletes) {
        m_legacyCredentials.remove(id);
    }

    m_pendingWrites.clear();
    m_pendingDeletes.clear();

    // Now safe to drop hidden rows entirely
    for (int i = m_connectionsTable->rowCount() - 1; i >= 0; --i) {
        if (m_connectionsTable->isRowHidden(i)) {
            m_connectionsTable->removeRow(i);
        }
    }

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
