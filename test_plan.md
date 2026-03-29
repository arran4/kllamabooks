# High-Level Plan

**Goal:** Implement a coherent, integrated design to handle active chat generation conflicts, defer and review document generations, and unify the queue/notification models.

1. **Database Schema Changes (Migration to userVersion 13):**
   - We need to clean up `queue` schema. We will stick to the term `status` (or `state`) rather than mixing `status` and `processing_id`. Let's use `state` ("pending", "processing", "completed", "error", "paused"). Note: Memory instructions mention: "queue table's `state` and `response` columns".
   - Add `response` column to `queue` table.
   - We need to drop `processing_id` and replace it with `state`, `processing_pid` and `response`. Actually we need to add `parent_id` (lineage) as well if needed. Let's design the new queue table.
   - Create `document_history` table: `id`, `document_id`, `action_type`, `old_content`, `new_content`, `timestamp`, `generation_id` (nullable). Or just `content`, `timestamp` etc. Wait, the prompt says: "Store old versions of the document before generation-driven changes are applied." We need a simple history table.

2. **Refactor BookDatabase.cpp & BookDatabase.h:**
   - Implement `userVersion 13` migration carefully with explicit SQLite steps.
   - Update `QueueItem` struct to reflect the new columns: `state`, `response`, `parent_id`, etc.
   - Update `enqueuePrompt`, `updateQueueItem`, `getQueueItems`, `deleteQueueItem` etc.

3. **Active Chat Generation Conflict Handling:**
   - In `MainWindow::onSendMessage()`, detect if there is already an actively processing item in the current chat path/folder.
   - If yes, show a custom dialog (or a message box with custom buttons) offering:
     - Queue to send later
     - Fork and send
     - Cancel previous generation and replace with this
     - Save to drafts
     - Ignore text and clear input
     - Cancel and leave prompt untouched
   - Implement these actions natively.

4. **Chat Context Menu Improvements:**
   - Add "Reuse this" to `MainWindow::onChatTextAreaContextMenu()`. Populates `inputField` or `multiLineInput` with the source text (preferring user prompt for assistant messages).

5. **Safe Document AI Generation & Review Flow:**
   - In `QueueManager::onComplete()`, if `targetType == "document"`, do NOT overwrite the document in `BookDatabase`.
   - Instead, save the generated text into the `response` field of the `queue` item and set its `state` to `completed`.
   - Emit a signal to show an "awaiting review" notification (e.g. `document_review`).
   - Create a unified `DocumentReviewDialog` or use an existing one to show Original vs Generated text, with options:
     - Replace
     - Append
     - Create new / copy / variant / fork
     - Regenerate (with editable prompt)
     - Discard
     - Edit generated text
   - Add entry points to this review flow from notifications, queue window, and right-clicking the document.
   - In `MainWindow::onQueueChunk()`, update the in-progress preview *without* mutating the database or the live `documentEditorView` text if possible, OR mutate a separate preview widget, OR mutate `documentEditorView` but do NOT auto-save it. The prompt says "Still provide a preview of the in-progress generated output in the context of the document workflow. The user must be able to continue viewing and interacting with the original document while the generated result exists separately." This suggests showing it in a split view or a separate pane, or keeping the original text and just appending/replacing it visually but allowing them to toggle back. Actually, the prompt says "The generated result should be editable on its own before final apply/discard actions." So maybe a separate tab or an overlay, or opening the review dialog *during* generation. Let's create a `Review Document Generation` pane/dialog.

6. **Unified Notifications & Change Records:**
   - Unify the naming for completion notifications. The memory says: `awaiting_review` for documents.
   - Change `QueueManager` to emit a canonical notification for documents.

7. **Queue Model & Persistence Unification:**
   - Replace `QueueItem::processingId` with `QueueItem::processingPid` and `QueueItem::state`.
   - Standardize states: `pending`, `processing`, `completed`, `failed`.

Let's refine this plan.
