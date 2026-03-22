#ifndef CHATSETTINGSDIALOG_H
#define CHATSETTINGSDIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QComboBox>
#include <QString>

class ChatSettingsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ChatSettingsDialog(const QString& initialSystemPrompt, const QString& initialSendBehavior, QWidget* parent = nullptr);
    ~ChatSettingsDialog() = default;

    QString getSystemPrompt() const;
    QString getSendBehavior() const;

   private:
    QTextEdit* m_systemPromptEdit;
    QComboBox* m_sendBehaviorCombo;
};

#endif  // CHATSETTINGSDIALOG_H
