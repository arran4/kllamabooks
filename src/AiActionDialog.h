#ifndef AIACTIONDIALOG_H
#define AIACTIONDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QString>
#include <QTextEdit>

class QFormLayout;

struct AiDynamicInputInfo {
    QString fullMatch;
    QString type;
    QString label;
    QWidget* widget = nullptr;

    bool operator==(const AiDynamicInputInfo& other) const {
        return fullMatch == other.fullMatch && type == other.type && label == other.label;
    }
    bool operator!=(const AiDynamicInputInfo& other) const { return !(*this == other); }
};

class AiActionDialog : public QDialog {
    Q_OBJECT
   public:
    explicit AiActionDialog(const QString& title, const QString& labelText, const QString& defaultTemplate,
                            const QString& contextText, QWidget* parent = nullptr);

    QString getPrompt() const;

   public slots:
    void accept() override;

   private slots:
    void updateDynamicInputs();

   private:
    QTextEdit* m_instructionEdit;
    QString m_contextText;
    QString m_finalPrompt;
    QFormLayout* m_dynamicInputsLayout;
    QList<AiDynamicInputInfo> m_currentDynamicInputs;
};

#endif  // AIACTIONDIALOG_H
