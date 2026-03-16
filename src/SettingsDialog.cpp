#include "SettingsDialog.h"

#include <QInputDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    resize(500, 300);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // LLM Section
    QLabel* llmLabel = new QLabel(tr("<b>LLM Connections</b>"), this);
    mainLayout->addWidget(llmLabel);

    m_connectionsTable = new QTableWidget(this);
    m_connectionsTable->setColumnCount(3);
    m_connectionsTable->setHorizontalHeaderLabels({tr("Name"), tr("URL"), tr("Auth Key")});
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
}

SettingsDialog::~SettingsDialog() {}

void SettingsDialog::loadConnections() {
    QVariantList connections = m_settings.value("llmConnections").toList();
    if (connections.isEmpty() && m_settings.contains("ollamaUrl")) {
        // Migration from old single URL setting
        QVariantMap defaultConn;
        defaultConn["name"] = "Default Ollama";
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
        m_connectionsTable->setItem(row, 1, new QTableWidgetItem(map["url"].toString()));
        m_connectionsTable->setItem(row, 2, new QTableWidgetItem(map["authKey"].toString()));
    }
}

void SettingsDialog::saveConnections() {
    QVariantList connections;
    for (int i = 0; i < m_connectionsTable->rowCount(); ++i) {
        QVariantMap map;
        map["name"] = m_connectionsTable->item(i, 0)->text();
        map["url"] = m_connectionsTable->item(i, 1)->text();
        map["authKey"] = m_connectionsTable->item(i, 2)->text();
        connections.append(map);
    }
    m_settings.setValue("llmConnections", connections);
}

void SettingsDialog::onAddConnection() {
    bool ok;
    QString name = QInputDialog::getText(this, tr("Add Connection"), tr("Connection Name:"), QLineEdit::Normal,
                                         "New Connection", &ok);
    if (!ok || name.isEmpty()) return;

    QString url =
        QInputDialog::getText(this, tr("Add Connection"), tr("URL:"), QLineEdit::Normal, "http://localhost:11434", &ok);
    if (!ok || url.isEmpty()) return;

    QString authKey =
        QInputDialog::getText(this, tr("Add Connection"), tr("Auth Key (optional):"), QLineEdit::Normal, "", &ok);
    if (!ok) return;

    int row = m_connectionsTable->rowCount();
    m_connectionsTable->insertRow(row);
    m_connectionsTable->setItem(row, 0, new QTableWidgetItem(name));
    m_connectionsTable->setItem(row, 1, new QTableWidgetItem(url));
    m_connectionsTable->setItem(row, 2, new QTableWidgetItem(authKey));
}

void SettingsDialog::onEditConnection() {
    int row = m_connectionsTable->currentRow();
    if (row < 0) return;

    bool ok;
    QString name = QInputDialog::getText(this, tr("Edit Connection"), tr("Connection Name:"), QLineEdit::Normal,
                                         m_connectionsTable->item(row, 0)->text(), &ok);
    if (!ok || name.isEmpty()) return;

    QString url = QInputDialog::getText(this, tr("Edit Connection"), tr("URL:"), QLineEdit::Normal,
                                        m_connectionsTable->item(row, 1)->text(), &ok);
    if (!ok || url.isEmpty()) return;

    QString authKey = QInputDialog::getText(this, tr("Edit Connection"), tr("Auth Key (optional):"), QLineEdit::Normal,
                                            m_connectionsTable->item(row, 2)->text(), &ok);
    if (!ok) return;

    m_connectionsTable->item(row, 0)->setText(name);
    m_connectionsTable->item(row, 1)->setText(url);
    m_connectionsTable->item(row, 2)->setText(authKey);
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
    QString urlStr = m_connectionsTable->item(row, 1)->text();
    QString authKey = m_connectionsTable->item(row, 2)->text();
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
    emit settingsApplied();
    accept();
}
