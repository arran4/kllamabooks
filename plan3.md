5. **PromptQueueManager interface**
   `void enqueueJob(const QString& bookFile, int userMsgId, int assistantMsgId, const QString& model, const QString& systemPrompt, const QString& prompt)`
   `void pauseQueue()`
   `void resumeQueue()`
   `void pauseJob(int jobId)`
   `void cancelJob(int jobId)`
   `void moveJobUp(int jobId)`
   `void moveJobDown(int jobId)`
   Signals: `jobStatusChanged(int jobId, Status newStatus)`, `chunkReceived(int jobId, const QString& chunk, const QString& fullText)`, `queueCountChanged(int count)`, `notificationAdded(const Notification& notif)`.

6. **Notification UI**
   - Add an icon on the tree items and list items. `openBooksTree` and `bookList`.
   - We can modify `QStandardItemModel` to show a notification icon using `Qt::DecorationRole` or a custom role with a delegate, but simply adding `QIcon` or appending a text like `*` or a specific emoji to the item name, or setting an icon that merges the folder/chat icon with a badge, might be easiest. Since it's a "physical marker against the element in the tree view and list view... different forms by type".
   - Using an overlay icon? Qt supports it natively via `QIcon::addPixmap()` but maybe just use an emoji in the text or a specific background color/font styling (`Qt::BackgroundRole`, `Qt::ForegroundRole`).
   - Actually, wait, "different forms by type". For example, responded = ✅, error = ❌. So we can update `Qt::DecorationRole` or append to the text? Wait, Qt has `QIcon` which can have multiple pixmaps, but an emoji in the text is robust and visible. Wait, the requirement says "physical marker against the element... different forms by type". Let's use an Emoji appended to the text, e.g., `[✅]` or `[❌]`.

7. **Queue Management Window**
   - A dialog or a panel shown when clicking the queue count in the status bar.
   - Contains a `QTableWidget` or `QListView` showing jobs.
   - Buttons: Pause Queue, Resume Queue, Cancel Job, Pause Job, Move Up, Move Down.
   - We need a `QueueWindow` dialog.

Let's check if the user asked for a "window with the contents of the queue... and a means to cancel/dismiss, or pause/hold certain items, along with pause the entire queue, and rearrange items". Yes.
