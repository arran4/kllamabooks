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
                                const QString& targetType = "document",
                                QWidget* parent = nullptr);
    ~DocumentEditWindow() override;

    void setInitialContent(const QString& content);

   signals:
    void documentModified(int documentId);
    void newDocumentCreated(int newDocumentId);

   protected:
    void closeEvent(QCloseEvent* event) override;

   private slots:
    void onSaveClicked();
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
    QString m_targetType;

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
};

#endif  // DOCUMENTEDITWINDOW_H
