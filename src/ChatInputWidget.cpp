#include "ChatInputWidget.h"

#include <QScrollBar>

ChatInputWidget::ChatInputWidget(QWidget* parent) : QTextEdit(parent), m_sendBehavior(EnterToSend) {
    // Set minimal height so it looks like an input field initially
    setAcceptRichText(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    // Connect text change signal to dynamically adjust size
    connect(this, &QTextEdit::textChanged, this, &ChatInputWidget::adjustHeightToContent);

    // Initial size adjustment
    adjustHeightToContent();
}

void ChatInputWidget::adjustHeightToContent() {
    int margins = contentsMargins().top() + contentsMargins().bottom() +
                  document()->documentMargin() * 2;
    int contentHeight = document()->size().height() + margins;

    // Set a sensible minimum and maximum height
    // Minimum: 1 line roughly, Maximum: half of parent or a sensible max
    int minHeight = fontMetrics().height() + margins + 10;
    int maxHeight = 300; // Sensible default max before scrolling

    if (parentWidget()) {
        maxHeight = parentWidget()->height() / 2;
    }

    int newHeight = qBound(minHeight, contentHeight, maxHeight);
    setFixedHeight(newHeight);
}

void ChatInputWidget::resizeEvent(QResizeEvent* event) {
    QTextEdit::resizeEvent(event);
    adjustHeightToContent();
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
