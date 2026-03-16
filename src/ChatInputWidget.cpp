#include "ChatInputWidget.h"

ChatInputWidget::ChatInputWidget(QWidget* parent) : QTextEdit(parent), m_sendBehavior(EnterToSend) {
    // Set minimal height so it looks like an input field initially
    setFixedHeight(60);
    setAcceptRichText(false);
}

ChatInputWidget::SendBehavior ChatInputWidget::sendBehavior() const { return m_sendBehavior; }

void ChatInputWidget::setSendBehavior(SendBehavior behavior) { m_sendBehavior = behavior; }

void ChatInputWidget::keyPressEvent(QKeyEvent* event) {
    bool hasModifier = (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier)) != 0;

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_sendBehavior == EnterToSend) {
            if (event->modifiers() & Qt::ShiftModifier) {
                // Shift+Enter creates a new line
                QTextEdit::keyPressEvent(event);
            } else {
                // Enter without shift sends
                emit returnPressed();
                // We consume the event
            }
        } else if (m_sendBehavior == CtrlEnterToSend) {
            if (event->modifiers() & Qt::ControlModifier) {
                // Ctrl+Enter sends
                emit returnPressed();
                // Consume
            } else {
                // Enter without ctrl creates a new line
                QTextEdit::keyPressEvent(event);
            }
        }
    } else {
        // Normal processing for other keys
        QTextEdit::keyPressEvent(event);
    }
}
