#ifndef AIOPERATIONSDIALOG_H
#define AIOPERATIONSDIALOG_H

#include <QDialog>
#include <QString>

class QComboBox;
class QTextEdit;

class AIOperationsDialog : public QDialog {
    Q_OBJECT
public:
    explicit AIOperationsDialog(const QString& defaultPrompt = "", QWidget* parent = nullptr);
    ~AIOperationsDialog() override;

    QString getOperation() const;
    QString getPrompt() const;
    QString getTargetAction() const;

private:
    QComboBox* m_operationCombo;
    QComboBox* m_targetActionCombo;
    QTextEdit* m_promptEdit;
};

#endif // AIOPERATIONSDIALOG_H
