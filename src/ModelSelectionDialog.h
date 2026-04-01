#ifndef MODELSELECTIONDIALOG_H
#define MODELSELECTIONDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStringList>

class ModelSelectionDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ModelSelectionDialog(const QStringList& models, const QStringList& previouslySelected = QStringList(),
                                  QWidget* parent = nullptr);
    ~ModelSelectionDialog();

    QStringList selectedModels() const;
    QString selectedModel() const;

   private slots:
    void onSearchChanged(const QString& text);
    void onItemSelected(QListWidgetItem* item);
    void onAccept();

   private:
    void setupUi();

    QLineEdit* m_searchField;
    QListWidget* m_modelList;
    QStringList m_allModels;
    QStringList m_previouslySelected;
    QString m_selectedModel;
};

#endif  // MODELSELECTIONDIALOG_H
