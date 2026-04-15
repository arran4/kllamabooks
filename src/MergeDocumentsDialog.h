#ifndef MERGEDOCUMENTSDIALOG_H
#define MERGEDOCUMENTSDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QList>
#include <QString>
#include <QTextEdit>
#include <QLineEdit>
#include <memory>
#include "BookDatabase.h"
#include "AIOperationsManager.h"
#include "AiActionDialog.h" // For AiDynamicInputInfo
#include "OllamaModelInfo.h"

class QFormLayout;
class ModelSelectionDialog;
class QPushButton;

class MergeDocumentsDialog : public QDialog {
    Q_OBJECT
   public:
    enum MergeAction {
        ReplaceExisting,
        NewDocument,
        ReplaceDocA,
        ReplaceDocB,
        ReplaceDocAandB
    };

    explicit MergeDocumentsDialog(BookDatabase* db, const QList<int>& documentIds, const QList<OllamaModelInfo>& modelInfos, const QStringList& fallbackModels, QWidget* parent = nullptr);

    QString getFinalPrompt() const;
    QStringList getSelectedModels() const;
    QString getRawPrompt() const;
    void setInitialPrompt(const QString& prompt);
    void setInitialModels(const QStringList& models);

    QString getTitle() const;
    void setTitle(const QString& title);
    void setDefaultTitleSuffix(const QString& suffix);

    void setIsRegenerating(bool isRegenerating);
    MergeAction getSelectedAction() const;

   public slots:
    void accept() override;

   private slots:
    void updateDynamicInputs();
    void onSelectModelsClicked();
    void onTemplateChanged(int index);
    void onHelpClicked();
    void onPreviewClicked();
    void onSaveTemplateClicked();

   private:
    void loadTemplates();

    BookDatabase* m_db;
    QList<int> m_documentIds;
    QList<OllamaModelInfo> m_modelInfos;
    QStringList m_fallbackModels;
    QStringList m_selectedModels;

    QString buildPreviewPrompt() const;

    QComboBox* m_templateCombo;
    QTextEdit* m_instructionEdit;
    QPushButton* m_selectModelsBtn;
    QLineEdit* m_titleEdit;

    QString m_defaultTitleSuffix;
    bool m_titleManuallyEdited = false;

    QString m_finalPrompt;
    QFormLayout* m_dynamicInputsLayout;
    QList<AiDynamicInputInfo> m_currentDynamicInputs;
    QList<AIOperation> m_templates;
    QList<QString> m_documentContents;
    QList<QString> m_documentTitles;

    QComboBox* m_actionTypeCombo;
    bool m_isRegenerating = false;
};

#endif // MERGEDOCUMENTSDIALOG_H