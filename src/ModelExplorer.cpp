#include "ModelExplorer.h"

#include <QAction>
#include <QDesktopServices>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

ModelExplorer::ModelExplorer(OllamaClient* client, bool isOllama, QWidget* parent) : QDialog(parent), m_client(client), m_isOllama(isOllama), m_networkManager(new QNetworkAccessManager(this)) {
    setWindowTitle("Model Explorer");
    resize(800, 600);

    loadFavorites();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    m_tabWidget = new QTabWidget(this);

    setupInstalledTab();
    setupDownloadTab();
    setupDownloadingTab();
    setupOnlineSearchTab();

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

    if (m_isOllama) {
        QHBoxLayout* externalSearchLayout = new QHBoxLayout();
        QPushButton* searchOllamaButton = new QPushButton("Search Ollama", this);
        QPushButton* searchHfButton = new QPushButton("Search HF.com", this);
        externalSearchLayout->addWidget(searchOllamaButton);
        externalSearchLayout->addWidget(searchHfButton);
        layout->addLayout(externalSearchLayout);

        QLabel* hfNoteLabel = new QLabel("Note: Downloading Hugging Face models may require configuring Ollama's HF connection (e.g., via HF CLI).", this);
        hfNoteLabel->setStyleSheet("color: gray; font-style: italic; font-size: 10px;");
        layout->addWidget(hfNoteLabel);

        connect(searchOllamaButton, &QPushButton::clicked, this, &ModelExplorer::onSearchOllamaClicked);
        connect(searchHfButton, &QPushButton::clicked, this, &ModelExplorer::onSearchHfClicked);
    }

    layout->addStretch();

    connect(m_downloadButton, &QPushButton::clicked, this, &ModelExplorer::onDownloadModelClicked);
    connect(m_downloadNameField, &QLineEdit::returnPressed, this, &ModelExplorer::onDownloadModelClicked);

    m_tabWidget->addTab(tab, "Download Models");
}

void ModelExplorer::setupOnlineSearchTab() {
    QWidget* tab = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(tab);

    QHBoxLayout* searchLayout = new QHBoxLayout();
    m_searchSourceCombo = new QComboBox(this);
    if (m_isOllama) {
        m_searchSourceCombo->addItem("Ollama");
        m_searchSourceCombo->addItem("Hugging Face");
    } else {
        m_searchSourceCombo->addItem("Hugging Face");
    }
    m_onlineSearchField = new QLineEdit(this);
    m_onlineSearchField->setPlaceholderText("Search query...");
    m_onlineSearchButton = new QPushButton("Search", this);

    searchLayout->addWidget(m_searchSourceCombo);
    searchLayout->addWidget(m_onlineSearchField);
    searchLayout->addWidget(m_onlineSearchButton);
    layout->addLayout(searchLayout);

    m_searchResultsTable = new QTableWidget(0, 2, this);
    m_searchResultsTable->setHorizontalHeaderLabels({"Model", "Description/Downloads"});
    m_searchResultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_searchResultsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_searchResultsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_searchResultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_searchResultsTable);

    QPushButton* downloadSelectedBtn = new QPushButton("Download Selected", this);
    layout->addWidget(downloadSelectedBtn);

    connect(m_onlineSearchButton, &QPushButton::clicked, this, &ModelExplorer::onOnlineSearchClicked);
    connect(m_onlineSearchField, &QLineEdit::returnPressed, this, &ModelExplorer::onOnlineSearchClicked);
    connect(downloadSelectedBtn, &QPushButton::clicked, this, &ModelExplorer::onDownloadFromSearchClicked);

    m_tabWidget->addTab(tab, "Online Search");
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

    QPushButton* clearButton = new QPushButton("Clear Finished", this);
    layout->addWidget(clearButton);
    connect(clearButton, &QPushButton::clicked, this, &ModelExplorer::onClearFinishedDownloadsClicked);

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

void ModelExplorer::onSearchOllamaClicked() {
    m_tabWidget->setCurrentIndex(3);
    if (m_searchSourceCombo->findText("Ollama") != -1) {
        m_searchSourceCombo->setCurrentText("Ollama");
    }
    m_onlineSearchField->setText(m_downloadNameField->text());
    onOnlineSearchClicked();
}

void ModelExplorer::onSearchHfClicked() {
    m_tabWidget->setCurrentIndex(3);
    if (m_searchSourceCombo->findText("Hugging Face") != -1) {
        m_searchSourceCombo->setCurrentText("Hugging Face");
    }
    m_onlineSearchField->setText(m_downloadNameField->text());
    onOnlineSearchClicked();
}

void ModelExplorer::onOnlineSearchClicked() {
    QString query = m_onlineSearchField->text().trimmed();
    if (query.isEmpty()) return;

    m_searchResultsTable->setRowCount(0);
    m_onlineSearchButton->setEnabled(false);
    m_onlineSearchButton->setText("Searching...");

    QString source = m_searchSourceCombo->currentText();
    if (source == "Ollama") {
        QUrl url("https://ollama.com/library");
        QUrlQuery urlQuery;
        urlQuery.addQueryItem("q", query);
        url.setQuery(urlQuery);

        QNetworkRequest request(url);
        QNetworkReply* reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (reply->error() == QNetworkReply::NoError) {
                QString html = QString::fromUtf8(reply->readAll());
                QRegularExpression nameRe("<span class=\"group-hover:underline truncate\">(.*?)</span>");
                QRegularExpression descRe("<p class=\"max-w-lg break-words text-neutral-800 text-md\">(.*?)</p>");

                QRegularExpressionMatchIterator nameIt = nameRe.globalMatch(html);
                QRegularExpressionMatchIterator descIt = descRe.globalMatch(html);

                while (nameIt.hasNext() && descIt.hasNext()) {
                    QString name = nameIt.next().captured(1);
                    QString desc = descIt.next().captured(1).trimmed();
                    int row = m_searchResultsTable->rowCount();
                    m_searchResultsTable->insertRow(row);
                    m_searchResultsTable->setItem(row, 0, new QTableWidgetItem(name));
                    m_searchResultsTable->setItem(row, 1, new QTableWidgetItem(desc));
                }
            }
            reply->deleteLater();
            m_onlineSearchButton->setEnabled(true);
            m_onlineSearchButton->setText("Search");
        });
    } else if (source == "Hugging Face") {
        QUrl url("https://huggingface.co/api/models");
        QUrlQuery urlQuery;
        urlQuery.addQueryItem("search", query);
        urlQuery.addQueryItem("limit", "20");
        url.setQuery(urlQuery);

        QNetworkRequest request(url);
        QNetworkReply* reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (reply->error() == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                if (doc.isArray()) {
                    QJsonArray arr = doc.array();
                    for (const QJsonValue& val : arr) {
                        QJsonObject obj = val.toObject();
                        QString name = obj["id"].toString();
                        QString desc = "Downloads: " + QString::number(obj["downloads"].toInt()) + ", Pipeline: " + obj["pipeline_tag"].toString();
                        int row = m_searchResultsTable->rowCount();
                        m_searchResultsTable->insertRow(row);
                        m_searchResultsTable->setItem(row, 0, new QTableWidgetItem(name));
                        m_searchResultsTable->setItem(row, 1, new QTableWidgetItem(desc));
                    }
                }
            }
            reply->deleteLater();
            m_onlineSearchButton->setEnabled(true);
            m_onlineSearchButton->setText("Search");
        });
    }
}

void ModelExplorer::onDownloadFromSearchClicked() {
    QList<QTableWidgetItem*> selection = m_searchResultsTable->selectedItems();
    if (selection.isEmpty()) return;

    int row = selection.first()->row();
    QString name = m_searchResultsTable->item(row, 0)->text();

    if (m_searchSourceCombo->currentText() == "Hugging Face") {
        name = "hf.co/" + name;
    }

    m_downloadNameField->setText(name);
    m_tabWidget->setCurrentIndex(1); // Switch back to Download Models tab
    onDownloadModelClicked();
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

void ModelExplorer::onPullFinished(const QString& modelName, bool success, const QString& errorString) {
    for (int row = 0; row < m_downloadingTable->rowCount(); ++row) {
        if (m_downloadingTable->item(row, 0)->text() == modelName) {
            if (success) {
                m_downloadingTable->item(row, 1)->setText("Completed");
                QProgressBar* progressBar = qobject_cast<QProgressBar*>(m_downloadingTable->cellWidget(row, 2));
                if (progressBar) {
                    progressBar->setValue(100);
                }
            } else {
                m_downloadingTable->item(row, 1)->setText("Error: " + errorString);
            }
            break;
        }
    }
    m_client->fetchModels();  // Refresh installed list
}

void ModelExplorer::onClearFinishedDownloadsClicked() {
    for (int row = m_downloadingTable->rowCount() - 1; row >= 0; --row) {
        QString status = m_downloadingTable->item(row, 1)->text();
        if (status == "Completed" || status.startsWith("Error:")) {
            m_downloadingTable->removeRow(row);
        }
    }
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
