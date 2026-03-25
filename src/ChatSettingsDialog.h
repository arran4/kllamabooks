#ifndef CHATSETTINGSDIALOG_H
#define CHATSETTINGSDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QString>
#include <QTextEdit>

class ChatSettingsDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ChatSettingsDialog(const QString& initialTitle, const QString& initialNotes,
                                const QString& initialSystemPrompt, const QString& initialSendBehavior,
                                const QString& initialModel, const QString& initialMultiLine,
                                const QString& endpointName, const QStringList& availableModels,
                                QWidget* parent = nullptr);
    ~ChatSettingsDialog() = default;

    QString getTitle() const;
    QString getNotes() const;
    QString getSystemPrompt() const;
    QString getSendBehavior() const;
    QString getModel() const;
    QString getMultiLine() const;

   private:
    QLineEdit* m_titleEdit;
    QTextEdit* m_notesEdit;
    QTextEdit* m_systemPromptEdit;
    QComboBox* m_sendBehaviorCombo;
    QComboBox* m_modelCombo;
    QComboBox* m_multiLineCombo;
};

#endif  // CHATSETTINGSDIALOG_H
