#ifndef RECOVERYBROWSERDIALOG_H
#define RECOVERYBROWSERDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>

#include "BookDatabase.h"

class RecoveryBrowserDialog : public QDialog {
    Q_OBJECT
   public:
    explicit RecoveryBrowserDialog(BookDatabase* db, QWidget* parent = nullptr);

   private slots:
    void refreshRecords();
    void onSelectionChanged();
    void onExportJson();
    void onReimportArtifact();
    void onDeletePermanently();

   private:
    int selectedRecordId() const;

    BookDatabase* m_db;
    QTableWidget* m_table;
    QTextEdit* m_detailView;
    QPushButton* m_exportBtn;
    QPushButton* m_reimportBtn;
    QPushButton* m_deleteBtn;
};

#endif  // RECOVERYBROWSERDIALOG_H
