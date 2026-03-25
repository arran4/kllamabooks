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
    explicit ChatSettingsDialog(const QString& initialTitle, const QString& initialSystemPrompt,
                                const QString& initialSendBehavior, const QString& initialModel,
                                const QString& initialMultiLine, const QString& initialDraftPrompt,
                                const QString& initialUserNote, const QString& endpointName,
                                const QStringList& availableModels, QWidget* parent = nullptr);
    ~ChatSettingsDialog() = default;

    QString getTitle() const;
    QString getSystemPrompt() const;
    QString getSendBehavior() const;
    QString getModel() const;
    QString getMultiLine() const;
    QString getDraftPrompt() const;
    QString getUserNote() const;

   private:
    QLineEdit* m_titleEdit;
    QTextEdit* m_systemPromptEdit;
    QComboBox* m_sendBehaviorCombo;
    QComboBox* m_modelCombo;
    QComboBox* m_multiLineCombo;
    QTextEdit* m_draftPromptEdit;
    QTextEdit* m_userNoteEdit;
};

#endif  // CHATSETTINGSDIALOG_H
