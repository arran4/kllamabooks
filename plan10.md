Oh! `currentDb->dismissNotificationByMessageId(tailNodeId);` happens when we view a chat node (in `updateLinearChatView`).
What if we view a document? Do we dismiss any notifications?
Wait, if `review_needed` is on the document, does viewing the document dismiss it?
No! `review_needed` MUST NOT be dismissed just by viewing the document. It must be dismissed by explicitly reviewing it.
The user's comment is: `I want a way of tracking "unread" here, ie things modified without viewing even if it's done via a notification`.

Ah, wait! If I open `DocumentReviewDialog` and click "Replace", the dialog calls `m_db->updateDocument(m_documentId, m_resultEdit->toPlainText())`.
At this point, the document has been "modified via a notification" (the user clicked it in the notification menu, and then clicked "Replace").
If they do that, the document is modified, but they haven't explicitly *viewed* the document in the central view!
And they want an "unread" notification on the document now!

Let's trace what happens when `DocumentReviewDialog` modifies the document:
```cpp
void DocumentReviewDialog::onReplace() {
    m_db->updateDocument(m_documentId, m_resultEdit->toPlainText());
    m_db->deleteQueueItem(m_queueItemId);
    accept();
}
```
If we add `m_db->addNotification(m_documentId, "unread");` here, then it will get an "unread" notification.
Wait, if they accept via the notification dialog, they DID see the content! But maybe they want to see it in the context of the app or remember they modified it.
Wait, "even if it's done via a notification". This strongly implies that performing the action via the notification should STILL mark it as "unread" or modified so they can track it in the tree!

What if `AIOperationsDialog` generated it via single-document synchronous generation (`MainWindow::onDocumentAIOperations`)? No, that directly opens `DocumentReviewDialog`?
Actually, `MainWindow::onDocumentAIOperations` queues the prompt via `enqueuePrompt`.
