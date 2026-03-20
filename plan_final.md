Let's design the queue and notification features.

1. **Job Queue Manager** (`PromptQueueManager.h / .cpp`)
   - Manages a central SQLite DB `QStandardPaths::AppDataLocation + "/queue.db"`
   - Table `jobs`: `id`, `book_file`, `prompt_text`, `model`, `system_prompt`, `parent_message_id`, `assistant_message_id`, `status` (pending, generating, paused, completed, error), `error_text`, `sort_order`, `created_at`.
   - Table `notifications`: `id`, `book_file`, `message_id`, `type` (responded, error), `text`, `dismissed`.
   - Has a timer or event loop. When a job is pending, starts it via `OllamaClient`.
   - While generating, it uses a background `BookDatabase` connection to update the chunk.
   - Emits signals: `queueUpdated`, `notificationAdded`, `jobStatusChanged(int jobId, Status newStatus)`.
   - If user cancels mid-generation: update state to Canceled/Error.

2. **MainWindow modifications**
   - Create `PromptQueueManager` instance.
   - Replace direct `ollamaClient.generate(...)` call with `promptQueueManager->enqueueJob(...)`.
   - Remove `m_isGenerating` blocking in UI, because jobs are now async in the background! Wait, does the UI still need to block if they are viewing the chat being generated?
   - The user asked to produce a "universal queue for all prompts, that will process the events in order. ... This is required as because of limited vmem and mem I can only run a certain number of jobs at a time."
   - If the queue runs in background, they can switch to other chats and write prompts! So the UI should NOT block input completely, but rather block input FOR THAT SPECIFIC CHAT only, or allow them to enqueue multiple prompts on the same chat (queued). Actually, the user says "The queue needs to exist in the database... indicator as to how many tasks in the queue." It implies they can queue up multiple tasks.
   - "There is a physical marker against the element in the tree view and list view that the element has a notification / update against it (different forms by type) there should also be a notification menu..."

3. **QueueWindow UI**
   - Dialog containing a TableView of jobs in the queue.
   - Buttons to interact with jobs (Pause, Cancel, Move Up, Move Down) and the queue (Pause all).

4. **Notifications UI**
   - Read from `notifications` table.
   - Menu triggered by a status bar button (which shows `🔔 X | 📋 Y jobs`).
   - When clicked, opens a menu or window showing unread notifications.
   - Clicking a notification navigates to that book and chat, and dismisses it.

Let's break it down into files:
- `PromptQueueManager.h` & `cpp`
- `QueueWindow.h` & `cpp`
- `MainWindow` modifications.

Let's ask for plan review on this structure.
