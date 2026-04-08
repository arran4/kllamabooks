#ifndef MERGEDOCUMENTSDIALOG_H
#define MERGEDOCUMENTSDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QList>
#include <QString>
#include <QTextEdit>
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
    explicit MergeDocumentsDialog(BookDatabase* db, const QList<int>& documentIds, const QList<OllamaModelInfo>& modelInfos, const QStringList& fallbackModels, QWidget* parent = nullptr);

    QString getFinalPrompt() const;
    QStringList getSelectedModels() const;

   public slots:
    void accept() override;

   private slots:
    void updateDynamicInputs();
    void onSelectModelsClicked();
    void onTemplateChanged(int index);
    void onHelpClicked();

   private:
    void loadTemplates();

    BookDatabase* m_db;
    QList<int> m_documentIds;
    QList<OllamaModelInfo> m_modelInfos;
    QStringList m_fallbackModels;
    QStringList m_selectedModels;

    QComboBox* m_templateCombo;
    QTextEdit* m_instructionEdit;
    QPushButton* m_selectModelsBtn;

    QString m_finalPrompt;
    QFormLayout* m_dynamicInputsLayout;
    QList<AiDynamicInputInfo> m_currentDynamicInputs;
    QList<AIOperation> m_templates;
    QList<QString> m_documentContents;
};

#endif // MERGEDOCUMENTSDIALOG_H