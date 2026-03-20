1. **Queue Database Design**
   - Add a `prompt_queue` table in `BookDatabase` schema.
     Columns: `id`, `book_filepath`, `prompt_text`, `model`, `system_prompt`, `state` (pending, generating, paused, completed, error), `result_node_id`, `created_at`. Wait, `prompt_queue` should probably be global since it manages LLM generation, or per-book? The requirement says "The queue needs to exist in the database, if I quit mid generation, when I reopen it the queue will start reprocessing the old items." If the queue is per-book, it wouldn't process items for closed books. The app runs global Ollama client, so maybe a global database or use the main app settings database if there is one? Currently, books are separate SQLite databases. We might need a central "app database" for the queue, or store queue items in a separate `kllamabooks_queue.db`. Let's create `kllamabooks_queue.db` in `AppDataLocation` or use `QSettings`? SQLite is better for queue state.
   - Wait, if a queue item creates a message in a specific book, we need to know which book, parent message ID, etc.
   - Let's create a `JobQueue` manager. It will store queue items in an SQLite database (e.g. `queue.db` in AppDataLocation).

2. **Job Queue Manager (`PromptQueueManager`)**
   - Creates/opens `queue.db`.
   - Table `jobs`: `id`, `book_path`, `parent_message_id`, `user_message_id`, `assistant_message_id` (created in advance), `prompt`, `model`, `system_prompt`, `status` (Pending, Generating, Paused, Completed, Error), `created_at`.
   - The queue manager runs a background timer or event loop to process jobs one by one. It talks to `OllamaClient`.
   - Supports max concurrent jobs setting (e.g., 1). The requirement says "can only run a certain number of jobs at a time." For now we can default to 1 and let users change it in settings, or just 1.

3. **Notification System**
   - Need a notification system: "Notification against the chat with a type of 'responded to' or 'error' Based on what happened. Once user dismisses it goes away."
   - "Physical marker against the element in the tree view and list view... different forms by type".
   - "Notification menu, that has a total amount of notifications... press it and navigate you to that item and automatically dismiss."
   - Notification count in the status bar -> press that to get menu.

4. **Queue UI**
   - "Status bar should be an indicator as to how many tasks in the queue."
   - "When I press it should see a window with the contents of the queue... cancel/dismiss, pause/hold certain items, pause entire queue, rearrange items."

Let me plan it out in more detail.
