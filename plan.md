# 1. Database Migrations (BookDatabase.cpp)
- Add new schema migration (version 13).
- Create `document_history` table:
    `id INTEGER PRIMARY KEY AUTOINCREMENT, document_id INTEGER, action_type TEXT, content TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP`
- Update `queue` table:
    `ALTER TABLE queue ADD COLUMN response TEXT DEFAULT '';`
    `ALTER TABLE queue ADD COLUMN state TEXT DEFAULT 'pending';`
    `ALTER TABLE queue ADD COLUMN parent_id INTEGER DEFAULT 0;`

# 2. Queue Model Unification (QueueManager / BookDatabase)
- Sync QueueItem struct in `BookDatabase.h` to have `state`, `response`, `parentId`.
- Update `enqueuePrompt` to insert `state`='pending' instead of using missing status.
- Update `updateQueueItem` and query functions to use `state` instead of `status`.
- Centralize `onComplete` in `QueueManager.cpp`:
  - For chat messages, update message and delete queue item. Add notification "responded_to".
  - For documents, do NOT update document directly. Instead, set queue item `state`='completed' and save output in `response` column. Delete from `m_isProcessing` but keep in DB.
  - Add notification "review_needed".

# 3. Active Chat Generation Conflict Handling (MainWindow.cpp)
- In `onSendMessage`, if `QueueManager::isProcessing()` is true AND it is generating for the current chat (check `currentLastNodeId` or chat path):
- Show a custom `QDialog` or `QMessageBox` with options:
    - Queue (auto send after)
    - Fork and send (creates a branch and queues)
    - Cancel previous and replace (calls `QueueManager::cancelItem`, creates new msg, queues)
    - Save to drafts (saves to DB and clears input)
    - Ignore and clear
    - Cancel (abort sending)

# 4. Chat Context Menu Improvements (MainWindow.cpp)
- In `createStandardContextMenu` block in `MainWindow::showItemContextMenu` (or equivalent for chat texts), add "Reuse this".
- If user right-clicks an assistant message, grab the text of the preceding user message to reuse.
- Put text in `inputField` or `multiLineInput`.

# 5. Safe Document AI Generation & Preview (QueueManager & MainWindow)
- `QueueManager::onChunk` should emit chunks but NOT save to DB documents.
- In `MainWindow`, subscribe to `QueueManager::processingChunk` and `processingFinished`.
- If a document is generating, display a banner or side view for the in-progress preview. The actual `QTextEdit` content remains unchanged.

# 6. Document Generation Review Flow
- Create a reusable unified review UI dialog or sidebar (`DocumentReviewDialog`).
- Triggered from "review_needed" notifications or queue double-click.
- Options: Replace, Append, Create new document (Fork), Regenerate (edit prompt), Discard.
- Centralize these actions: e.g., Replace applies `response` to `document` and stores old version in `document_history`. Discard removes queue item.

# 7. Pre-commit
- Complete pre-commit steps to ensure proper testing, verification, review, and reflection are done.
