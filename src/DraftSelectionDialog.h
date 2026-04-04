#ifndef DRAFTSELECTIONDIALOG_H
#define DRAFTSELECTIONDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QTextEdit>
#include <QSplitter>
#include "BookDatabase.h"

class DraftSelectionDialog : public QDialog {
    Q_OBJECT

public:
    enum Action { Cancel, UseDraft, EditOriginal };

    explicit DraftSelectionDialog(std::shared_ptr<BookDatabase> db, const QString& originalContent, const QList<DocumentNode>& drafts, QWidget* parent = nullptr);

    Action getSelectedAction() const;
    DocumentNode getSelectedDraft() const;

private slots:
    void onDraftSelectionChanged();
    void onUseDraft();
    void onDeleteDraft();
    void onEditOriginal();
    void onShowDifferences();

private:
    std::shared_ptr<BookDatabase> m_db;
    QString m_originalContent;
    QList<DocumentNode> m_drafts;
    Action m_selectedAction = Cancel;
    DocumentNode m_selectedDraft;

    QListWidget* m_draftList;
    QTextEdit* m_previewEdit;
    QPushButton* m_useBtn;
    QPushButton* m_deleteBtn;
    QPushButton* m_diffBtn;
};

#endif // DRAFTSELECTIONDIALOG_H
