So in `onOpenBooksSelectionChanged` (line ~3254) and `onOpenBooksTreeDoubleClicked` (line ~4278), it opens the document and sets it as `currentDocumentId`.
If we added an "unread" notification, we could dismiss it here by calling `currentDb->dismissNotificationByMessageId(currentDocumentId)` (assuming we use `messageId` as the document ID for "unread" notifications).
Wait! `dismissNotificationByMessageId(currentDocumentId)` will delete ALL notifications with `messageId == currentDocumentId`.
Wait, if `type == "review_needed"`, its `messageId` is the `queueItemId`, which is NOT the document ID (although queue item ID might accidentally equal the document ID! Oh no. But `dismissNotificationByMessageId` deletes all notifications where `messageId == id`. That means if a queue item happens to have ID 5, and document has ID 5, viewing document 5 will dismiss `review_needed` for queue item 5!

Let's look at `dismissNotificationByMessageId` in `BookDatabase.cpp`.
