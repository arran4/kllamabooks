#ifndef CHATSETTINGSDIALOG_H
#define CHATSETTINGSDIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QString>

class ChatSettingsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ChatSettingsDialog(const QString& initialSystemPrompt, QWidget* parent = nullptr);
    ~ChatSettingsDialog() = default;

    QString getSystemPrompt() const;

   private:
    QTextEdit* m_systemPromptEdit;
};

#endif  // CHATSETTINGSDIALOG_H
