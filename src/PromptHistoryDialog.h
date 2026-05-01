#ifndef PROMPTHISTORYDIALOG_H
#define PROMPTHISTORYDIALOG_H

#include <QDialog>
#include <memory>

#include "BookDatabase.h"

class QListWidget;
class QTextEdit;

class PromptHistoryDialog : public QDialog {
    Q_OBJECT
   public:
    explicit PromptHistoryDialog(std::shared_ptr<BookDatabase> db, int documentId, QWidget* parent = nullptr);
    ~PromptHistoryDialog() override;

   private slots:
    void onSelectionChanged();

   private:
    std::shared_ptr<BookDatabase> m_db;
    int m_documentId;

    QListWidget* m_historyList;
    QTextEdit* m_contentView;

    QList<BookDatabase::PromptHistoryEntry> m_entries;

    void loadHistory();
};

#endif // PROMPTHISTORYDIALOG_H
