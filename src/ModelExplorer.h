#ifndef MODELEXPLORER_H
#define MODELEXPLORER_H

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "OllamaClient.h"

class ModelExplorer : public QDialog {
    Q_OBJECT
   public:
    explicit ModelExplorer(OllamaClient* client, QWidget* parent = nullptr);
    ~ModelExplorer();

   private slots:
    void onSearchInstalledClicked();
    void onDownloadModelClicked();
    void updateModelList(const QStringList& models);
    void onPullProgressUpdated(const QString& modelName, int percent, const QString& status);
    void onPullFinished(const QString& modelName, bool success, const QString& errorString);
    void showInstalledContextMenu(const QPoint& pos);
    void onClearFinishedDownloadsClicked();

   private:
    OllamaClient* m_client;

    QTabWidget* m_tabWidget;

    // Installed Tab
    QLineEdit* m_installedSearchField;
    QListWidget* m_installedList;

    // Download Tab
    QLineEdit* m_downloadNameField;
    QPushButton* m_downloadButton;

    // Downloading Tab
    QTableWidget* m_downloadingTable;

    void setupInstalledTab();
    void setupDownloadTab();
    void setupDownloadingTab();
    void loadFavorites();
    void saveFavorites();

    QStringList m_favorites;
};

#endif  // MODELEXPLORER_H
