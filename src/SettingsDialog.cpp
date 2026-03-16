#include "SettingsDialog.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    resize(400, 200);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // LLM Section
    QLabel* llmLabel = new QLabel(tr("<b>LLM Settings</b>"), this);
    mainLayout->addWidget(llmLabel);

    QHBoxLayout* urlLayout = new QHBoxLayout();
    QLabel* urlLabel = new QLabel(tr("Ollama URL:"), this);
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setText(m_settings.value("ollamaUrl", "http://localhost:11434").toString());
    urlLayout->addWidget(urlLabel);
    urlLayout->addWidget(m_urlEdit);
    mainLayout->addLayout(urlLayout);

    QHBoxLayout* testLayout = new QHBoxLayout();
    m_testButton = new QPushButton(tr("Test Connection"), this);
    testLayout->addStretch();
    testLayout->addWidget(m_testButton);
    mainLayout->addLayout(testLayout);

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
    connect(m_testButton, &QPushButton::clicked, this, &SettingsDialog::onTestConnection);
    connect(m_applyButton, &QPushButton::clicked, this, &SettingsDialog::onApply);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

SettingsDialog::~SettingsDialog() {}

void SettingsDialog::onTestConnection() {
    m_testButton->setEnabled(false);

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QString urlStr = m_urlEdit->text();
    if (!urlStr.endsWith("/")) urlStr += "/";
    urlStr += "api/tags";

    QNetworkRequest request((QUrl(urlStr)));
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
    m_settings.setValue("ollamaUrl", m_urlEdit->text());
    emit settingsApplied();
    accept();
}
