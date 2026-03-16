#ifndef MODELSELECTIONDIALOG_H
#define MODELSELECTIONDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

class ModelSelectionDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ModelSelectionDialog(const QStringList& models, QWidget* parent = nullptr);
    ~ModelSelectionDialog();

    QString selectedModel() const;

   private slots:
    void onSearchChanged(const QString& text);
    void onItemSelected(QListWidgetItem* item);

   private:
    void setupUi();

    QLineEdit* m_searchField;
    QListWidget* m_modelList;
    QStringList m_allModels;
    QString m_selectedModel;
};

#endif  // MODELSELECTIONDIALOG_H
