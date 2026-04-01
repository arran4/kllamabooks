#ifndef AIACTIONDIALOG_H
#define AIACTIONDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QString>
#include <QTextEdit>

class AiActionDialog : public QDialog {
    Q_OBJECT
   public:
    explicit AiActionDialog(const QString& title, const QString& labelText, const QString& defaultTemplate,
                            const QString& contextText, QWidget* parent = nullptr);

    QString getPrompt() const;

   public slots:
    void accept() override;

   private:
    QTextEdit* m_instructionEdit;
    QString m_contextText;
    QString m_finalPrompt;
};

#endif  // AIACTIONDIALOG_H
