#ifndef MODELEXPLORER_H
#define MODELEXPLORER_H

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "OllamaClient.h"
#include "OllamaModelInfo.h"

class ModelExplorer : public QDialog {
    Q_OBJECT
   public:
    explicit ModelExplorer(OllamaClient* client, bool isOllama = false, QWidget* parent = nullptr);
    ~ModelExplorer();

   private slots:
    void onSearchInstalledClicked();
    void onSearchOllamaClicked();
    void onSearchHfClicked();
    void onDownloadFromSearchClicked();
    void onDownloadModelClicked();
    void updateModelList(const QList<OllamaModelInfo>& models);
    void onOnlineSearchClicked();
    void onLoadMoreClicked();
    void onPullProgressUpdated(const QString& modelName, int percent, const QString& status);
    void onPullFinished(const QString& modelName, bool success, const QString& errorString);
    void showInstalledContextMenu(const QPoint& pos);
    void onClearFinishedDownloadsClicked();

   private:
    OllamaClient* m_client;
    bool m_isOllama;

    QNetworkAccessManager* m_networkManager;

    QTabWidget* m_tabWidget;

    // Installed Tab
    QLineEdit* m_installedSearchField;
    QTableWidget* m_installedTable;

    // Download Tab
    QLineEdit* m_downloadNameField;
    QPushButton* m_downloadButton;

    // Downloading Tab
    QTableWidget* m_downloadingTable;

    // Online Search Tab
    QComboBox* m_searchSourceCombo;
    QLineEdit* m_onlineSearchField;
    QPushButton* m_onlineSearchButton;
    QTableWidget* m_searchResultsTable;
    QPushButton* m_loadMoreButton;

    int m_hfSkipCount = 0;

    void setupInstalledTab();
    void setupDownloadTab();
    void setupDownloadingTab();
    void setupOnlineSearchTab();
    void loadFavorites();
    void saveFavorites();

    QStringList m_favorites;
};

#endif  // MODELEXPLORER_H
