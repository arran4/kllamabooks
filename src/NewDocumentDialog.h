#ifndef NEWDOCUMENTDIALOG_H
#define NEWDOCUMENTDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <memory>

#include "OllamaModelInfo.h"

class QComboBox;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QTreeWidget;
class QTreeWidgetItem;
class BookDatabase;

class NewDocumentDialog : public QDialog {
    Q_OBJECT
   public:
    enum DocumentType { FromTemplate, FromPrompt, ResumeDraft };

    explicit NewDocumentDialog(std::shared_ptr<BookDatabase> db, int defaultFolderId,
                               const QList<OllamaModelInfo>& modelInfos, const QStringList& fallbackModels,
                               QComboBox* mainEndpointComboBox, const QStringList& initialModels = QStringList(), QWidget* parent = nullptr);

    DocumentType getDocumentType() const;
    QString getTitle() const;
    int getSelectedFolderId() const;
    QString getPrompt() const;
    QString getSelectedTemplateId() const;
    int getSelectedDraftId() const;
    bool isOverwriteDocument() const;
    int getOverwriteDocumentId() const;
    QStringList getSelectedModels() const;
    int getSelectedEndpointIndex() const;

   private slots:
    void onTypeChanged(int index);
    void onOverwriteToggled(int state);
    void onSelectModelsClicked();

   private:
    void populateFolders(QTreeWidgetItem* parentItem, int parentId);
    void populateTemplates();
    void populateDrafts();
    void populateDocuments();
    void updateModelButtonText();

    std::shared_ptr<BookDatabase> m_db;
    int m_defaultFolderId;
    QList<OllamaModelInfo> m_modelInfos;
    QStringList m_fallbackModels;
    QStringList m_selectedModels;

    QComboBox* m_typeCombo;
    QLineEdit* m_titleEdit;

    // Dynamic widgets
    QWidget* m_promptWidget;
    QComboBox* m_endpointCombo;
    QPushButton* m_selectModelsBtn;
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
