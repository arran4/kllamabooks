Requirements mapping:

Queue DB:
1. `PromptQueueManager` singleton or owned by MainWindow. Uses a local SQLite DB for queue jobs. `QStandardPaths::AppDataLocation + "/queue.db"`.
2. Tables: `queue` (id, book_file, model, system_prompt, prompt, parent_id, user_message_id, assistant_message_id, status (pending, generating, paused, error, completed), error_message, created_at, sort_order).
3. We need a way to track the total queue, reorder, pause/resume item, cancel.
4. Notifications: "Notification against the chat with a type of 'responded to' or 'error' Based on what happened. Once user dismisses it goes away."
   - Table `notifications`: `id`, `book_file`, `message_id` (the assistant message), `type` (responded, error), `text`, `dismissed`.

Queue Processing:
- Read pending/generating jobs in order of sort_order. Generating jobs on startup should be reset to pending.
- Run X jobs at once. For now default to 1 concurrent job (with an option to pause entire queue).
- Pass Ollama chunks to `BookDatabase` using the assistant_message_id. Since the user might be looking at the chat, or have the book closed!
- WAIT! If the book is closed, how do we write to it? The book is encrypted with a password!
- Ah! If a queue job targets an encrypted book, and the book is NOT open, we CANNOT write to it unless we have the password.
  - Option A: Only process queue jobs for currently OPEN books. (If a book is closed, its jobs stay pending, or we show a message).
  - Option B: We have WalletManager, which might have the password. `WalletManager::loadPassword(fileName)` can get the password. So `PromptQueueManager` can open the BookDatabase temporarily to write chunks, or keep it open.
  - If we write chunks periodically, opening/closing is expensive. The `PromptQueueManager` could use a temporary BookDatabase instance if the MainWindow doesn't have it open, or just use the MainWindow's currentDb if it matches.

Let's look at how MainWindow currently handles it:
- MainWindow calls `ollamaClient.generate(...)` and the lambda captures `currentDb` (via `this` pointer implicitly or explicitly) and `aiId`. The lambda updates `chatTextArea` and `currentDb->updateMessage(aiId, fullResponse);`.

If we move generation to `PromptQueueManager`:
- When user clicks Send:
  1. Add user message to BookDatabase.
  2. Add empty assistant message to BookDatabase.
  3. Enqueue job into `PromptQueueManager`.
  4. Display user message and empty assistant message in UI.
- `PromptQueueManager` picks up the job.
- Calls `ollamaClient.generate()`.
- On chunk:
  - `PromptQueueManager` needs to update the `BookDatabase`. It needs a `BookDatabase` instance. It can emit a signal `chunkReceived(jobId, bookFile, assistantMessageId, chunk, fullText)`.
  - `MainWindow` can listen to this signal. If the `bookFile` is the `currentDb`, it updates `chatTextArea` and `currentDb->updateMessage(...)`.
  - WAIT! If the user closes the book, `MainWindow` won't update the DB. So `PromptQueueManager` MUST handle updating the DB itself, or MainWindow must manage multiple DBs.
  - Actually, `BookDatabase` could be cached by `PromptQueueManager`, or we only run jobs for open books? The requirement says "The queue needs to exist in the database, if I quit mid generation, when I reopen it the queue will start reprocessing the old items." It does not explicitly mention processing closed books, but it implies resuming. When we reopen, the book might not be open yet. If the user uses a password, it might prompt? Let's process the queue regardless if WalletManager has the password, OR only process jobs for currently open books. If a book requires a password and WalletManager doesn't have it, we can mark the job as Error/Paused.

Let's look at WalletManager.
