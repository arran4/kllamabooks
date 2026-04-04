#ifndef MODELSELECTIONDIALOG_H
#define MODELSELECTIONDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>
#include <QPushButton>

#include "OllamaModelInfo.h"

class ModelSelectionDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ModelSelectionDialog(const QList<OllamaModelInfo>& modelInfos, const QStringList& fallbackModels, QWidget* parent = nullptr);
    ~ModelSelectionDialog();

    QString selectedModel() const;

   private slots:
    void onSearchChanged(const QString& text);
    void onItemSelected(QTableWidgetItem* item);

   private:
    void setupUi();

    QLineEdit* m_searchField;
    QTableWidget* m_modelTable;
    QList<OllamaModelInfo> m_modelInfos;
    QStringList m_fallbackModels;
    QString m_selectedModel;
};

#endif  // MODELSELECTIONDIALOG_H
