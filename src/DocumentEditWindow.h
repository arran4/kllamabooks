#ifndef DOCUMENTEDITWINDOW_H
#define DOCUMENTEDITWINDOW_H

#include <KXmlGuiWindow>
#include <QDateTime>
#include <QString>
#include <memory>

#include "BookDatabase.h"

class QTextEdit;
class QLabel;

class DocumentEditWindow : public KXmlGuiWindow {
    Q_OBJECT
   public:
    explicit DocumentEditWindow(std::shared_ptr<BookDatabase> db, int documentId, const QString& title,
                                const QString& itemType = "document", QWidget* parent = nullptr);
    ~DocumentEditWindow() override;

    void setContent(const QString& content);

   signals:
    void documentModified(int documentId);
    void newDocumentCreated(int newDocumentId);
    void jumpToDocumentRequested(int documentId);

   protected:
    void closeEvent(QCloseEvent* event) override;

   private slots:
    void onSaveClicked();
    void onJumpClicked();
    void onSaveAsClicked();
    void onSaveAsDraftClicked();
    void onRenameClicked();
    void onContentChanged();

   private:
    std::shared_ptr<BookDatabase> m_db;
    int m_documentId;
    QString m_title;
    QDateTime m_openTimestamp;
    QString m_initialContent;
    QString m_itemType;

    QTextEdit* m_editor;
    QLabel* m_statusLabel;
    QLabel* m_wordCountLabel;

    void setupWindow();
    void updateStatusBar();
    QDateTime getLatestDbTimestamp() const;
    void loadDocument();
    bool saveToDb();
    int forkDocument(const QString& newTitle);
    int saveToDraft(const QString& newTitle);
    QString getOriginalContent() const;
};

#endif  // DOCUMENTEDITWINDOW_H
