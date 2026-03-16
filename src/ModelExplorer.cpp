#include "ModelExplorer.h"

#include <QAction>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>

ModelExplorer::ModelExplorer(OllamaClient* client, QWidget* parent) : QDialog(parent), m_client(client) {
    setWindowTitle("Model Explorer");
    resize(800, 600);

    loadFavorites();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);

    setupInstalledTab();
    setupDownloadTab();
    setupDownloadingTab();

    mainLayout->addWidget(m_tabWidget);

    connect(m_client, &OllamaClient::modelListUpdated, this, &ModelExplorer::updateModelList);
    connect(m_client, &OllamaClient::pullProgressUpdated, this, &ModelExplorer::onPullProgressUpdated);
    connect(m_client, &OllamaClient::pullFinished, this, &ModelExplorer::onPullFinished);

    m_client->fetchModels();
}

ModelExplorer::~ModelExplorer() {}

void ModelExplorer::setupInstalledTab() {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(tab);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    m_installedSearchField = new QLineEdit(this);
    m_installedSearchField->setPlaceholderText("Search installed models...");
    QPushButton* searchButton = new QPushButton("Search", this);
    searchLayout->addWidget(m_installedSearchField);
    searchLayout->addWidget(searchButton);
    layout->addLayout(searchLayout);

    m_installedList = new QListWidget(this);
    m_installedList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_installedList, &QWidget::customContextMenuRequested, this, &ModelExplorer::showInstalledContextMenu);
    layout->addWidget(m_installedList);

    connect(searchButton, &QPushButton::clicked, this, &ModelExplorer::onSearchInstalledClicked);
    connect(m_installedSearchField, &QLineEdit::returnPressed, this, &ModelExplorer::onSearchInstalledClicked);

    m_tabWidget->addTab(tab, "Installed Models");
}

void ModelExplorer::setupDownloadTab() {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(tab);

    QHBoxLayout* downloadLayout = new QHBoxLayout();
    m_downloadNameField = new QLineEdit(this);
    m_downloadNameField->setPlaceholderText("Enter model name to pull (e.g. llama3:latest)");
    m_downloadButton = new QPushButton("Download", this);
    downloadLayout->addWidget(m_downloadNameField);
    downloadLayout->addWidget(m_downloadButton);

    layout->addLayout(downloadLayout);
    layout->addStretch();

    connect(m_downloadButton, &QPushButton::clicked, this, &ModelExplorer::onDownloadModelClicked);
    connect(m_downloadNameField, &QLineEdit::returnPressed, this, &ModelExplorer::onDownloadModelClicked);

    m_tabWidget->addTab(tab, "Download Models");
}

void ModelExplorer::setupDownloadingTab() {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(tab);

    m_downloadingTable = new QTableWidget(0, 3, this);
    m_downloadingTable->setHorizontalHeaderLabels({"Model", "Status", "Progress"});
    m_downloadingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_downloadingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_downloadingTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_downloadingTable->setSelectionMode(QAbstractItemView::NoSelection);

    layout->addWidget(m_downloadingTable);

    m_tabWidget->addTab(tab, "Downloading");
}

void ModelExplorer::loadFavorites() {
    QSettings settings;
    m_favorites = settings.value("favoriteModels").toStringList();
}

void ModelExplorer::saveFavorites() {
    QSettings settings;
    settings.setValue("favoriteModels", m_favorites);
}

void ModelExplorer::onSearchInstalledClicked() {
    QString query = m_installedSearchField->text().toLower();
    for (int i = 0; i < m_installedList->count(); ++i) {
        QListWidgetItem* item = m_installedList->item(i);
        QString text = item->text();
        // Remove favorite star when checking
        if (text.startsWith("⭐ ")) text = text.mid(2);
        item->setHidden(!text.toLower().contains(query));
    }
}

void ModelExplorer::onDownloadModelClicked() {
    QString modelName = m_downloadNameField->text().trimmed();
    if (modelName.isEmpty()) return;

    m_client->pullModel(modelName);

    // Check if it already exists in the table to avoid duplicates
    bool exists = false;
    for (int row = 0; row < m_downloadingTable->rowCount(); ++row) {
        if (m_downloadingTable->item(row, 0)->text() == modelName) {
            exists = true;
            break;
        }
    }

    if (!exists) {
        int row = m_downloadingTable->rowCount();
        m_downloadingTable->insertRow(row);
        m_downloadingTable->setItem(row, 0, new QTableWidgetItem(modelName));
        m_downloadingTable->setItem(row, 1, new QTableWidgetItem("Starting..."));

        QProgressBar* progressBar = new QProgressBar();
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        m_downloadingTable->setCellWidget(row, 2, progressBar);
    }

    m_downloadNameField->clear();
    m_tabWidget->setCurrentIndex(2);  // Switch to downloading tab
}

void ModelExplorer::updateModelList(const QStringList& models) {
    m_installedList->clear();

    // Add favorites first
    for (const QString& model : models) {
        if (m_favorites.contains(model)) {
            QListWidgetItem* item = new QListWidgetItem("⭐ " + model);
            item->setData(Qt::UserRole, model);
            m_installedList->addItem(item);
        }
    }

    // Add the rest
    for (const QString& model : models) {
        if (!m_favorites.contains(model)) {
            QListWidgetItem* item = new QListWidgetItem(model);
            item->setData(Qt::UserRole, model);
            m_installedList->addItem(item);
        }
    }
}

void ModelExplorer::onPullProgressUpdated(const QString& modelName, int percent, const QString& status) {
    for (int row = 0; row < m_downloadingTable->rowCount(); ++row) {
        if (m_downloadingTable->item(row, 0)->text() == modelName) {
            m_downloadingTable->item(row, 1)->setText(status);
            QProgressBar* progressBar = qobject_cast<QProgressBar*>(m_downloadingTable->cellWidget(row, 2));
            if (progressBar) {
                progressBar->setValue(percent);
            }
            return;
        }
    }
}

void ModelExplorer::onPullFinished(const QString& modelName) {
    for (int row = 0; row < m_downloadingTable->rowCount(); ++row) {
        if (m_downloadingTable->item(row, 0)->text() == modelName) {
            m_downloadingTable->removeRow(row);
            break;
        }
    }
    m_client->fetchModels();  // Refresh installed list
}

void ModelExplorer::showInstalledContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_installedList->itemAt(pos);
    if (!item) return;

    QString modelName = item->data(Qt::UserRole).toString();
    bool isFavorite = m_favorites.contains(modelName);

    QMenu menu(this);
    QAction* favoriteAction = menu.addAction(isFavorite ? "Unfavorite" : "Favorite");

    QAction* selectedAction = menu.exec(m_installedList->viewport()->mapToGlobal(pos));
    if (selectedAction == favoriteAction) {
        if (isFavorite) {
            m_favorites.removeAll(modelName);
        } else {
            m_favorites.append(modelName);
        }
        saveFavorites();
        m_client->fetchModels();  // Trigger a list refresh to sort properly
    }
}
