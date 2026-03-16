#include "ModelExplorer.h"
#include <QMessageBox>

ModelExplorer::ModelExplorer(OllamaClient* client, QWidget *parent)
    : QDialog(parent), m_client(client) {

    setWindowTitle("Model Explorer");
    resize(400, 300);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    m_searchField = new QLineEdit(this);
    m_searchField->setPlaceholderText("Search for models...");
    QPushButton* searchButton = new QPushButton("Search", this);
    searchLayout->addWidget(m_searchField);
    searchLayout->addWidget(searchButton);
    mainLayout->addLayout(searchLayout);

    m_modelList = new QListWidget(this);
    mainLayout->addWidget(m_modelList);

    QHBoxLayout* actionLayout = new QHBoxLayout();
    m_downloadButton = new QPushButton("Download", this);
    m_downloadButton->setEnabled(false);
    actionLayout->addWidget(m_downloadButton);
    mainLayout->addLayout(actionLayout);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(this);
    mainLayout->addWidget(m_statusLabel);

    connect(searchButton, &QPushButton::clicked, this, &ModelExplorer::onSearchClicked);
    connect(m_downloadButton, &QPushButton::clicked, this, &ModelExplorer::onDownloadClicked);
    connect(m_modelList, &QListWidget::itemSelectionChanged, [this]() {
        m_downloadButton->setEnabled(m_modelList->selectedItems().count() > 0);
    });

    connect(m_client, &OllamaClient::modelListUpdated, this, &ModelExplorer::updateModelList);
    // TODO: Connect download progress signals from OllamaClient if supported

    // Fetch initially available local models
    m_client->fetchModels();
}

ModelExplorer::~ModelExplorer() {}

void ModelExplorer::onSearchClicked() {
    // For now, this just filters the locally fetched list
    // A full implementation would need to query the Ollama library API
    QString query = m_searchField->text().toLower();
    for (int i = 0; i < m_modelList->count(); ++i) {
        QListWidgetItem* item = m_modelList->item(i);
        item->setHidden(!item->text().toLower().contains(query));
    }
}

void ModelExplorer::onDownloadClicked() {
    auto selectedItems = m_modelList->selectedItems();
    if (selectedItems.isEmpty()) return;

    QString modelName = selectedItems.first()->text();

    // Here we would call an Ollama endpoint to pull the model.
    // e.g. m_client->pullModel(modelName);

    m_progressBar->setVisible(true);
    m_statusLabel->setText(QString("Downloading %1...").arg(modelName));
    m_downloadButton->setEnabled(false);

    // Simulate progress for now
    m_statusLabel->setText("Download not fully implemented yet.");
    m_progressBar->setValue(100);
}

void ModelExplorer::updateModelList(const QStringList& models) {
    m_modelList->clear();
    for (const QString& model : models) {
        m_modelList->addItem(model);
    }
}

void ModelExplorer::onDownloadProgress(int percentage) {
    m_progressBar->setValue(percentage);
}

void ModelExplorer::onDownloadFinished() {
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Download complete.");
    m_downloadButton->setEnabled(true);
    m_client->fetchModels(); // Refresh list
}
