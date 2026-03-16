#ifndef CHATINPUTWIDGET_H
#define CHATINPUTWIDGET_H

#include <QKeyEvent>
#include <QTextEdit>

class ChatInputWidget : public QTextEdit {
    Q_OBJECT
   public:
    enum SendBehavior { EnterToSend, CtrlEnterToSend };

    explicit ChatInputWidget(QWidget* parent = nullptr);

    SendBehavior sendBehavior() const;
    void setSendBehavior(SendBehavior behavior);

   signals:
    void returnPressed();

   protected:
    void keyPressEvent(QKeyEvent* event) override;

   private:
    SendBehavior m_sendBehavior;
};

#endif  // CHATINPUTWIDGET_H
