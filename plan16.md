Ah, the reviewer is right! In `BookDatabase`, the IDs for documents, chat nodes, and queue items are completely separate sequences.
If we dismiss by message ID, we might dismiss a `responded_to` notification for a chat node with the same ID as the document!

I need to implement a new method `bool dismissNotificationByMessageIdAndType(int messageId, const QString& type);` in `BookDatabase.cpp` and `BookDatabase.h`.

Let's do that.
