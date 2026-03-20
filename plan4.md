Let's figure out how `PromptQueueManager` will interact with `BookDatabase`s.
It can have a list of cached `BookDatabase` pointers. Since `MainWindow` also has `currentDb` (`std::unique_ptr<BookDatabase>`), we can't share ownership easily unless we change `currentDb` to `std::shared_ptr` or let `PromptQueueManager` manage its own instances.
Wait, `MainWindow` closes `currentDb` when the user closes the book.
If `PromptQueueManager` is processing a job for a closed book, it can just create a temporary `BookDatabase` on the stack or heap, open it, do the work, and close it.
Wait, if it takes a while to generate, holding the DB open is better, but maybe just append at the end?
No, we need to save chunks so if it crashes we don't lose the whole generation.
Wait, `updateMessage` runs an SQL UPDATE on every chunk in MainWindow currently!
If we do that with a newly opened `BookDatabase` on every chunk, it's very slow.
So `PromptQueueManager` should keep the `BookDatabase` open while generating.
But wait, what if `MainWindow` has it open? SQLite handles concurrent connections gracefully, but since `BookDatabase` uses SQLCipher, can we have two connections to the SAME database file from the same process? Yes, SQLCipher supports concurrent connections.
However, caching `std::shared_ptr<BookDatabase>` might be cleaner.
Let's modify `MainWindow` to pass a `std::shared_ptr<BookDatabase>` to `PromptQueueManager`, or maybe `PromptQueueManager` is just owned by `MainWindow` and emits signals, and `MainWindow` passes chunks to `BookDatabase`?
NO! "The queue needs to exist in the database, if I quit mid generation, when I reopen it the queue will start reprocessing the old items."
So `PromptQueueManager` should be mostly independent.
Let's make `PromptQueueManager` create its own `BookDatabase` instance internally for the job it is currently processing. It loads the password via `WalletManager`.
If `WalletManager` doesn't have the password, it fails (Error state).

Let's refine `PromptQueueManager`.

8. Notifications
- When a job completes, `PromptQueueManager` saves a notification in its `queue.db`.
- The `Notification` table: `id, book_file, chat_id, type (responded, error), message, dismissed`.
- A menu on the status bar or somewhere shows notifications.
- When clicking a notification, it navigates to the item.
- To navigate to an item, `MainWindow` needs `openBook` and `selectChatNode`.
