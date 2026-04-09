#ifndef DRAFTSELECTIONDIALOG_H
#define DRAFTSELECTIONDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <memory>

#include "BookDatabase.h"

class QListWidget;
class QTextBrowser;
class QTextEdit;
class QSplitter;

class DraftSelectionDialog : public QDialog {
    Q_OBJECT
   public:
    explicit DraftSelectionDialog(std::shared_ptr<BookDatabase> db, int documentId, const QList<DocumentNode>& drafts,
                                  const QString& targetType = "document", QWidget* parent = nullptr);
    ~DraftSelectionDialog() override;

    QString getSelectedDraftContent() const;
    bool didSelectDraft() const;

   private slots:
    void onSelectionChanged();
    void onUseThis();
    void onIgnoreDrafts();
    void onDeleteView();
    void onPreview();
    void onShowDifferences();
    void refreshDraftsList();

   private:
    std::shared_ptr<BookDatabase> m_db;
    int m_documentId;
    QList<DocumentNode> m_drafts;
    QString m_targetType;

    QListWidget* m_draftsList;
    QTextBrowser* m_previewBrowser;
    QSplitter* m_mainSplitter;

    bool m_draftSelected;
    QString m_selectedContent;

    // Helper functions
    QString getOriginalContent() const;
    QString getCurrentDocumentContent() const;
};

#endif  // DRAFTSELECTIONDIALOG_H
