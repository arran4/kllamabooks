#ifndef NEWDOCUMENTDIALOG_H
#define NEWDOCUMENTDIALOG_H

#include <QDialog>
#include <QString>
#include <memory>

class QComboBox;
class QLineEdit;
class QTextEdit;
class QTreeWidget;
class QTreeWidgetItem;
class BookDatabase;

class NewDocumentDialog : public QDialog {
    Q_OBJECT
   public:
    enum DocumentType { Empty, FromPrompt, FromTemplate, ResumeDraft };

    explicit NewDocumentDialog(std::shared_ptr<BookDatabase> db, int defaultFolderId, QWidget* parent = nullptr);

    DocumentType getDocumentType() const;
    QString getTitle() const;
    int getSelectedFolderId() const;
    QString getPrompt() const;
    int getSelectedTemplateId() const;
    int getSelectedDraftId() const;
    bool isOverwriteDocument() const;
    int getOverwriteDocumentId() const;

   private slots:
    void onTypeChanged(int index);
    void onOverwriteToggled(int state);

   private:
    void populateFolders(QTreeWidgetItem* parentItem, int parentId);
    void populateTemplates();
    void populateDrafts();
    void populateDocuments();

    std::shared_ptr<BookDatabase> m_db;
    int m_defaultFolderId;

    QComboBox* m_typeCombo;
    QLineEdit* m_titleEdit;

    // Dynamic widgets
    QWidget* m_promptWidget;
    QTextEdit* m_promptEdit;

    QWidget* m_templateWidget;
    QComboBox* m_templateCombo;

    QWidget* m_draftWidget;
    QComboBox* m_draftCombo;
    class QCheckBox* m_overwriteCheck;
    QComboBox* m_documentCombo;

    QTreeWidget* m_folderTree;
};

#endif  // NEWDOCUMENTDIALOG_H
