#ifndef DOCUMENTREVIEWDIALOG_H
#define DOCUMENTREVIEWDIALOG_H

#include <QDialog>
#include <QString>
#include <memory>
#include "BookDatabase.h"

class QTextEdit;
class QPushButton;
class QComboBox;

class DocumentReviewDialog : public QDialog {
    Q_OBJECT
public:
    explicit DocumentReviewDialog(std::shared_ptr<BookDatabase> db, int queueItemId, QWidget* parent = nullptr);
    ~DocumentReviewDialog() override;

private slots:
    void onReplace();
    void onAppend();
    void onFork();
    void onRegenerate();
    void onDiscard();

private:
    std::shared_ptr<BookDatabase> m_db;
    int m_queueItemId;
    int m_documentId;
    QString m_originalPrompt;

    QTextEdit* m_promptEdit;
    QTextEdit* m_resultEdit;

    void loadData();
    void finalizeAndClose(bool deleteQueueItem);
};

#endif // DOCUMENTREVIEWDIALOG_H
