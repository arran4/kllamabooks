#ifndef MODELEXPLORER_H
#define MODELEXPLORER_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QProgressBar>
#include "OllamaClient.h"

class ModelExplorer : public QDialog {
    Q_OBJECT
public:
    explicit ModelExplorer(OllamaClient* client, QWidget *parent = nullptr);
    ~ModelExplorer();

private slots:
    void onSearchClicked();
    void onDownloadClicked();
    void updateModelList(const QStringList& models);
    void onDownloadProgress(int percentage);
    void onDownloadFinished();

private:
    OllamaClient* m_client;
    QLineEdit* m_searchField;
    QListWidget* m_modelList;
    QPushButton* m_downloadButton;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
};

#endif // MODELEXPLORER_H
