#ifndef DOCUMENTHISTORYDIALOG_H
#define DOCUMENTHISTORYDIALOG_H

#include <QDialog>
#include <memory>

#include "BookDatabase.h"

class QListWidget;
class QTextEdit;

class DocumentHistoryDialog : public QDialog {
    Q_OBJECT
   public:
    explicit DocumentHistoryDialog(std::shared_ptr<BookDatabase> db, int documentId, QWidget* parent = nullptr);
    ~DocumentHistoryDialog() override;

   private slots:
    void onSelectionChanged();
    void onRestore();
    void onViewPrompt();

   private:
    std::shared_ptr<BookDatabase> m_db;
    int m_documentId;

    QListWidget* m_historyList;
    QTextEdit* m_contentView;
    class QPushButton* m_viewPromptBtn;

    struct HistoryEntry {
        int id;
        QString actionType;
        QString content;
        QString timestamp;
        QString prompt;
    };
    QList<HistoryEntry> m_entries;

    void loadHistory();
};

#endif  // DOCUMENTHISTORYDIALOG_H
