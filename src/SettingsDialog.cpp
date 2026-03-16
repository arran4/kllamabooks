#include "SettingsDialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

ConnectionDialog::ConnectionDialog(QWidget* parent, const QString& name, const QString& backend, const QString& url,
                                   const QString& authKey)
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
    resize(500, 300);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Chat Settings Section
    QLabel* chatSettingsLabel = new QLabel(tr("<b>Chat Settings</b>"), this);
    mainLayout->addWidget(chatSettingsLabel);

    QHBoxLayout* sendBehaviorLayout = new QHBoxLayout();
    QLabel* sendBehaviorLabel = new QLabel(tr("Global Send Behavior:"), this);
    m_sendBehaviorCombo = new QComboBox(this);
    m_sendBehaviorCombo->addItem(tr("Enter to Send, Shift+Enter for Newline"), "EnterToSend");
    m_sendBehaviorCombo->addItem(tr("Ctrl+Enter to Send, Enter for Newline"), "CtrlEnterToSend");
    sendBehaviorLayout->addWidget(sendBehaviorLabel);
    sendBehaviorLayout->addWidget(m_sendBehaviorCombo);
    sendBehaviorLayout->addStretch();
    mainLayout->addLayout(sendBehaviorLayout);

    mainLayout->addSpacing(10);

    // LLM Section
    QLabel* llmLabel = new QLabel(tr("<b>LLM Connections</b>"), this);
    mainLayout->addWidget(llmLabel);

    m_connectionsTable = new QTableWidget(this);
    m_connectionsTable->setColumnCount(4);
    m_connectionsTable->setHorizontalHeaderLabels({tr("Name"), tr("Backend"), tr("URL"), tr("Auth Key")});
    m_connectionsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_connectionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_connectionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_connectionsTable);

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
    mainLayout->addLayout(btnManageLayout);

    mainLayout->addStretch();

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

    QString globalBehavior = m_settings.value("globalSendBehavior", "EnterToSend").toString();
    int index = m_sendBehaviorCombo->findData(globalBehavior);
    if (index >= 0) {
        m_sendBehaviorCombo->setCurrentIndex(index);
    }
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
    }
}

void SettingsDialog::onEditConnection() {
    int row = m_connectionsTable->currentRow();
    if (row < 0) return;

    QString name = m_connectionsTable->item(row, 0)->text();
    QString backend = m_connectionsTable->item(row, 1)->text();
    QString url = m_connectionsTable->item(row, 2)->text();
    QString authKey = m_connectionsTable->item(row, 3)->text();

    ConnectionDialog dialog(this, name, backend, url, authKey);
    if (dialog.exec() == QDialog::Accepted) {
        m_connectionsTable->item(row, 0)->setText(dialog.name());
        m_connectionsTable->item(row, 1)->setText(dialog.backend());
        m_connectionsTable->item(row, 2)->setText(dialog.url());
        m_connectionsTable->item(row, 3)->setText(dialog.authKey());
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
    m_settings.setValue("globalSendBehavior", selectedBehavior);

    emit settingsApplied();
    accept();
}
