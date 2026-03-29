1. **Schema Update (userVersion = 13)**
   - Add `state` (TEXT, e.g., "pending", "processing", "completed", "error", "paused") to `queue` table to replace logic relying on `processing_id > 0`. Wait, let's keep `processing_pid` as required by memory: "To handle concurrent application instances, KLlamaBooks uses a `processing_pid` column in the `queue` table as a lock for items in the 'processing' state. To prevent orphaned tasks while avoiding PID reuse collisions...". Wait, the prompt says: "Choose one canonical field name for queue lifecycle state, either `state` or `status`, but not both." I will add `state` column.
   - Add `response` column to `queue` table.
   - Add `parent_id` column to `queue` table for lineage context, if applicable. Wait, maybe `target_id` vs `message_id` is sufficient.
   - Create `document_history` table: `id`, `document_id`, `action_type`, `old_content`, `new_content`, `timestamp`.

2. **Refactor BookDatabase Queue Persistence**
   - Update `QueueItem` struct with `state` and `response` fields.
   - Remove `status` and `processing_id` from existing queue model and only use `state` and `processing_pid`. Actually, there is `processing_id` now, memory says we should keep `processing_pid`, but wait, the prompt says "Do not keep both `state` and `status`." In v11 there is `processing_id` and no `status`. I'll rename `processing_id` back to `processing_pid` or just add `state`.
   - Update `queue` queries to use `state`.
   - Handle migrations cleanly.

3. **Active Chat Generation Conflict Handling (`MainWindow::onSendMessage`)**
   - Check if the chat is actively generating. A chat is actively generating if there's a queue item with `state == "processing"` for its `messageId` or any node in `currentChatPath`.
   - If generating, pop up a dialog asking what to do:
      - Queue to send later
      - Fork and send
      - Cancel previous and replace
      - Save to drafts
      - Ignore
      - Cancel
   - Implement handlers for each choice. For "Cancel previous", use `OllamaClient::abortGenerations()` and update queue state.

4. **Chat Context Menu: "Reuse this"**
   - In `MainWindow::onChatTextAreaContextMenu()`, add "Reuse this" action.
   - Find the right text (if assistant, look up the parent user prompt), populate input field.

5. **Safe Document AI Generation & Review (`QueueManager` & `MainWindow`)**
   - When generating a document, do *not* append directly to `documentEditorView` or `BookDatabase` via `updateDocument`.
   - Provide visual feedback during generation: maybe add a separate "Preview" text edit overlay, or append it to the document visually but don't save it to DB, or just use a floating dialog. Wait, the prompt says "The user must be able to continue viewing and interacting with the original document while the generated result exists separately". So appending visually to the document editor is bad. We could show the preview in a separate dialog or an inline widget. I will add an inline `QTextEdit` or split the view to show the generation preview, or just don't show it inline and use the Global Spy / Queue Window. Or just a non-modal dialog for generation preview.
   - Wait, `onQueueChunk`: if `targetType == "document"`, we could store the text in `queue`'s `response` and show a preview window, or just leave it in the queue window. The prompt says: "Still provide a preview of the in-progress generated output in the context of the document workflow". We could have a "Document AI Preview" dock or split pane in `MainWindow`.
   - When generation completes (`onComplete`), set queue state to `completed`, save response in DB, emit canonical `document_review` notification.
   - Provide unified `DocumentReviewDialog`. It shows Original Text, Generated Text, and actions (Replace, Append, New Document, Regenerate, Discard). This is triggered from the notification click or document context menu.

6. **Unified Notifications**
   - Use `awaiting_review` for document generation completion.
   - Clean up notifications when an action is taken.

7. **Cancellation Behavior**
   - Enhance `QueueManager::cancelItem` to abort actual network generation if it's currently processing (`m_client->abortGenerations()`).
