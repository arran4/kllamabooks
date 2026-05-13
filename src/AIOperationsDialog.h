#ifndef AIOPERATIONSDIALOG_H
#define AIOPERATIONSDIALOG_H

#include <QDialog>
#include <QString>

#include "OllamaModelInfo.h"

class QComboBox;
class QPushButton;
class QTextEdit;
class BookDatabase;
class QFormLayout;
class QSplitter;
class QScrollArea;

struct DynamicInputInfo {
    QString fullMatch;
    QString type;
    QString label;
    QWidget* widget = nullptr;

    bool operator==(const DynamicInputInfo& other) const {
        return fullMatch == other.fullMatch && type == other.type && label == other.label;
    }
    bool operator!=(const DynamicInputInfo& other) const { return !(*this == other); }
};

class AIOperationsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit AIOperationsDialog(BookDatabase* db, const QString& defaultPrompt,
                                const QList<OllamaModelInfo>& modelInfos,
                                const QStringList& fallbackModels, QWidget* parent = nullptr);
    ~AIOperationsDialog() override;

    QString getOperation() const;
    QString getPrompt() const;
    QStringList getSelectedModels() const;

    void setForkOnlyMode(bool enabled);

   public slots:
    void accept() override;

   private slots:
    void updateDynamicInputs();
    void onSelectModelsClicked();
    void onSaveDraftClicked();
    void onRestoreDraftClicked();

   private:
    BookDatabase* m_db;
    QList<OllamaModelInfo> m_modelInfos;
    QStringList m_fallbackModels;
    QStringList m_selectedModels;

    QComboBox* m_operationCombo;
    QTextEdit* m_promptEdit;
    QFormLayout* m_dynamicInputsLayout;
    QList<DynamicInputInfo> m_currentDynamicInputs;
    QString m_finalPrompt;
    QPushButton* m_selectModelsBtn;

    QSplitter* m_mainSplitter;

    void updateModelButtonText();
};

#endif  // AIOPERATIONSDIALOG_H
