#ifndef AIOPERATIONSDIALOG_H
#define AIOPERATIONSDIALOG_H

#include <QDialog>
#include <QString>

class QComboBox;
class QTextEdit;
class BookDatabase;

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

   private:
    QComboBox* m_operationCombo;
    QTextEdit* m_promptEdit;
    QString m_finalPrompt;
};

#endif  // AIOPERATIONSDIALOG_H
