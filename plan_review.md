1. **Create `PromptQueueManager`**
   - Implement an independent `PromptQueueManager` class (using a local `kllamabooks_queue.db`) to manage LLM tasks.
   - Database schema includes `jobs` (book_filepath, model, system_prompt, prompt_text, user_message_id, assistant_message_id, status, error_text, sort_order) and `notifications` (book_filepath, assistant_message_id, type, dismissed).
   - The manager will run a background loop/timer to process one "Pending" job at a time using `OllamaClient`.
   - On processing, it will open the target `BookDatabase` (fetching the password via `WalletManager`), send chunks to the DB, and emit signals for `MainWindow` to update the UI if the user is viewing that chat.
   - Upon completion or error, it will create a `Notification` and emit a signal.

2. **UI Enhancements for Queue & Notifications**
   - Add a "Tasks in Queue: X" indicator and a "Notifications: Y" indicator to the `QStatusBar`.
   - Clicking the queue indicator opens a `QueueWindow` showing a list/table of jobs with actions: Pause/Resume Queue, Pause/Resume Job, Cancel Job, Move Up/Down.
   - Clicking the notifications indicator opens a menu listing unread notifications. Clicking a notification will navigate the user to that specific book and chat node, and dismiss the notification.

3. **Tree View Markers**
   - In `MainWindow`, update the `QStandardItemModel` (`openBooksModel` and `bookList`) to display a physical marker (e.g., an emoji like `[✅]` or `[❌]` appended to the text, or a specific icon badge via `Qt::DecorationRole`) when a notification exists for that item or its children.

4. **Integration with `MainWindow`**
   - Replace the direct `ollamaClient.generate()` call in `onSendMessage()` with enqueuing a job to `PromptQueueManager`.
   - Update `MainWindow` to listen to chunk updates and notification signals from `PromptQueueManager` to update the active view and tree markers.

5. **Pre-commit Steps**
   - Ensure proper testing, verification, review, and reflection are done.
