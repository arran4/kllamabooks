#ifndef AIOPERATIONSDIALOG_H
#define AIOPERATIONSDIALOG_H

#include <QDialog>
#include <QString>

class QComboBox;
class QTextEdit;
class BookDatabase;
class QFormLayout;

struct DynamicInputInfo {
    QString fullMatch;
    QString type;
    QString label;
    QWidget* widget = nullptr;

    bool operator==(const DynamicInputInfo& other) const {
        return fullMatch == other.fullMatch && type == other.type && label == other.label;
    }
    bool operator!=(const DynamicInputInfo& other) const {
        return !(*this == other);
    }
};

class AIOperationsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit AIOperationsDialog(BookDatabase* db, const QString& defaultPrompt = "", QWidget* parent = nullptr);
    ~AIOperationsDialog() override;

    QString getOperation() const;
    QString getPrompt() const;

    void setForkOnlyMode(bool enabled);

   public slots:
    void accept() override;

   private slots:
    void updateDynamicInputs();

   private:
    QComboBox* m_operationCombo;
    QTextEdit* m_promptEdit;
    QFormLayout* m_dynamicInputsLayout;
    QList<DynamicInputInfo> m_currentDynamicInputs;
    QString m_finalPrompt;
};

#endif  // AIOPERATIONSDIALOG_H
