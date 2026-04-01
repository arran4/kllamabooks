Wait! The user says: "I want a way of tracking 'unread' here, ie things modified without viewing even if it's done via a notification".
If I look at `DocumentReviewDialog.cpp`, after `updateDocument`, the document is modified. Since they clicked "Replace" or "Append" from the notification dialog, they modified it without viewing the document itself in the main window!
So they want to track these modified documents as "unread".

Wait, maybe `unread` is a new notification type! Let's say `unread` is type 3 (maybe a different color, or just reuse `review_needed`? No, `review_needed` is deleted when `deleteQueueItem` is called via `db->dismissNotification(n.id)` which is called in `MainWindow` AFTER `reviewDlg.exec()`).
Wait, `m_db->addNotification(m_documentId, "unread");` could be added in `DocumentReviewDialog` when `updateDocument` is called!

Then in `MainWindow::updateNotificationStatus()`, we can also check for `n.type == "unread"`.
Wait! If `n.type == "unread"`, the `messageId` is the `m_documentId` (the actual document ID)!
Let's see if we can do this:
In `DocumentReviewDialog::onReplace()`:
```cpp
    m_db->updateDocument(m_documentId, title, newContent);
    m_db->addNotification(m_documentId, "unread");
    finalizeAndClose(true);
```
And in `onAppend()`:
```cpp
    m_db->updateDocument(m_documentId, title, oldContent + additionalContent);
    m_db->addNotification(m_documentId, "unread");
    finalizeAndClose(true);
```
And what if they `onFork()`? Fork creates a NEW document. Should the new document be marked "unread"?
```cpp
void DocumentReviewDialog::onFork() {
    int newId = m_db->addDocument(folderId, title + " (Fork)", newContent, m_documentId);
    m_db->addNotification(newId, "unread");
    finalizeAndClose(true);
}
```
Wait, how does "unread" get dismissed?
It should be dismissed when the document is viewed. Where is the document viewed?
`MainWindow::getDocumentContent` or `MainWindow::onOpenBooksTreeDoubleClicked`.
Let's check `MainWindow::onOpenBooksTreeDoubleClicked`.
