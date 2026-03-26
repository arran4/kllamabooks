import re

with open("src/MainWindow.cpp", "r") as f:
    text = f.read()

def replace_comment(old_comment, new_comment):
    global text
    if old_comment in text:
        text = text.replace(old_comment, new_comment)
    else:
        print(f"Could not find:\n{old_comment}")

replace_comment(
"""/**
 * @brief Handles double-click events on the main tree view to open documents or chats.
 *
 * @param index The model index of the double-clicked item.
 */""",
"""/**
 * @brief Intercepts double-click interactions on the left-pane navigation tree (openBooksTree).
 *
 * This function resolves the clicked QModelIndex to its underlying QStandardItem and evaluates its type.
 * If the type is a text-editable item (document, template, draft), it triggers the editor stack
 * (docContainer) and loads the raw text content into the QTextEdit surface.
 * If the type is a chat-related node, it instead activates the chat viewing stack (chatWindowView)
 * and populates the history pane linearly up to that node's state.
 *
 * @param index The QModelIndex of the item that was double-clicked in the QTreeView.
 */""")

replace_comment(
"""/**
 * @brief Handles double-click events in the virtual file system explorer view.
 *
 * @param index The model index of the double-clicked item.
 */""",
"""/**
 * @brief Handles double-click navigation within the central QListView file explorer (vfsExplorer).
 *
 * The virtual file system explorer dynamically represents the contents of the currently selected
 * folder in the main tree view. When a user double-clicks an item in this view, this function:
 * 1. Checks if the item is the specialized ".." (up one directory) node and navigates the tree up.
 * 2. If it's a regular item, it searches the main openBooksTree model using findItemInTree to
 *    locate the corresponding absolute tree node and forcefully selects it, effectively sinking
 *    the user deeper into the folder structure.
 *
 * @param index The model index from the vfsExplorer list view.
 */""")

replace_comment(
"""/**
 * @brief Saves the currently active document or draft to the database.
 */""",
"""/**
 * @brief Persists the active document editor state back to the SQLite database.
 *
 * Activated by the 'Save Document' button or equivalent shortcuts. It evaluates whether the
 * document is a newly minted unsaved buffer (isCreatingNewDoc) or an existing file being updated.
 * It resolves the target parent folder ID and utilizes the respective BookDatabase method
 * (addDocument, addTemplate, updateDocument, etc.) based on the item type metadata.
 * If the document was being auto-saved as a transient draft, the temporary draft entry is purged
 * upon successful explicit save.
 */""")

replace_comment(
"""/**
 * @brief Saves the currently active note or draft to the database.
 */""",
"""/**
 * @brief Persists the active note editor state back to the SQLite database.
 *
 * Parallel in structure to onSaveDocBtnClicked, but exclusively targets the 'notes' schema.
 * Determines if a new note record needs to be created or if an existing note (currentNoteId)
 * should be updated. It identically cleans up any lingering auto-drafts associated with the session.
 */""")

replace_comment(
"""/**
 * @brief Shows a custom context menu for selected text in the chat window to quickly create nodes.
 *
 * @param pos The position where the context menu was requested.
 */""",
"""/**
 * @brief Spawns a contextual interaction menu when users right-click highlighted text inside the chat viewer.
 *
 * This quality-of-life feature allows users to rapidly extract valuable snippets from a conversation
 * stream and natively fork them into standalone items. The selected text is aggressively sanitized
 * (e.g., replacing Qt's paragraph separators 0x2029 with standard newlines).
 * The menu exposes distinct actions to create a new Document, Note, or a fresh Chat Session branch,
 * using the extracted snippet as the initializing content. The new items are placed inside their
 * respective root folders in the active BookDatabase.
 *
 * @param pos The local widget coordinates where the right-click occurred.
 */""")

replace_comment(
"""/**
 * @brief Handles double-click events in the chat fork explorer to switch to a specific branch.
 *
 * @param index The model index of the double-clicked item.
 */""",
"""/**
 * @brief Synchronizes the main UI state to a specific chat branch when clicked in the fork explorer.
 *
 * The fork explorer (a list view showing alternative branches off a specific message node) allows
 * nonlinear navigation of AI conversations. When a branch is selected here, this function:
 * 1. Updates the global `currentLastNodeId` tracker.
 * 2. Redraws the main text area (updateLinearChatView) to reflect the history leading up to that node.
 * 3. Bidirectionally syncs the main navigation tree (openBooksTree) to select the correct corresponding item.
 *
 * @param index The model index of the clicked fork branch.
 */""")

replace_comment(
"""/**
 * @brief Shows a context menu in the chat fork explorer for options like branching or prompting.
 *
 * @param pos The position where the context menu was requested.
 */""",
"""/**
 * @brief Provides management options for specific conversation forks via right-click menu.
 *
 * Operates on items within the `chatForkExplorer` view. Exposes the ability to modify the metadata
 * of a specific conversational timeline, most notably overriding the internal System Prompt
 * instructions just for that distinct branch, saving the mutation back to the database.
 *
 * @param pos The local widget coordinates for menu spawning.
 */""")

replace_comment(
"""/**
 * @brief Global event filter for the main window, intercepting drags, drops, and mouse events.
 *
 * @param obj The object that received the event.
 * @param event The event being filtered.
 * @return true if the event was handled and consumed, false otherwise.
 */""",
"""/**
 * @brief Application-wide event interceptor funnel used predominantly for complex drag-and-drop routing.
 *
 * Because Qt's standard item views struggle with sophisticated cross-view drops and external OS
 * file injections natively, this filter overrides QEvent::DragEnter, QEvent::DragMove, and QEvent::Drop
 * globally for `openBooksTree` and `vfsExplorer`.
 *
 * It manages:
 * - External file imports (dropping a .db file opens it, dropping .md/.txt natively imports it
 *   into the hovered database folder).
 * - Internal item dragging (intercepting application/x-qabstractitemmodeldatalist MimeData) to
 *   translate visual drops into robust database move/copy SQL transactions using moveItemToFolder.
 *
 * @param obj The pointer to the QObject receiving the event (usually a viewport).
 * @param event The QEvent pointer containing the interaction state.
 * @return true if the event was manually processed and should be halted, false to allow native Qt handling.
 */""")

replace_comment(
"""/**
 * @brief Executes a drag-and-drop move or copy operation between database folders.
 *
 * @param draggedItem The standard item being moved.
 * @param targetItem The destination folder item.
 * @param isCopy True if this is a duplication operation, false for a standard move.
 * @return true if the operation succeeded, false otherwise.
 */""",
"""/**
 * @brief Validates and executes internal item relocation across the virtual folder hierarchy.
 *
 * Fired by the eventFilter drop handlers. This function acts as the gatekeeper for structural
 * integrity. It strictly verifies that items are only dropped into matching folder schemas
 * (e.g., 'document' items can only go to 'docs_folder').
 *
 * Once validated, it routes the operation to the target BookDatabase to execute the underlying
 * SQL updates. If `isCopy` is true (triggered via Ctrl+Drop), it duplicates the records. Finally,
 * it directly manipulates the QStandardItemModel rows to visually reflect the move instantly without
 * requiring a heavy database reload query.
 *
 * @param draggedItem The physical QStandardItem pointer that was dragged.
 * @param targetItem The underlying QStandardItem pointer representing the drop destination.
 * @param isCopy Boolean flag indicating if the operation should duplicate rather than move.
 * @return true if the operation was valid and executed, false if rejected.
 */""")

replace_comment(
"""/**
 * @brief Submits a user message from the input fields to the current chat session and triggers generation.
 */""",
"""/**
 * @brief Core dispatch mechanism for ingesting user input and initiating the LLM generation cycle.
 *
 * Executed when the user hits the Enter key or Send button.
 * 1. Resolves whether to pull text from the single-line input field or the multi-line editor box.
 * 2. Enforces validation (e.g., ensuring a model is selected).
 * 3. Commits the raw user string to the SQLite database as a new 'user' role message node.
 *    - If starting a new session (currentLastNodeId == 0), it generates a short title and
 *      places it in the correct chats folder.
 *    - If continuing an existing session, it appends it as a child of the current leaf node.
 * 4. Pushes the newly created interaction state to the QueueManager to begin background network processing.
 * 5. Safely cleans the input buffers and synchronizes the UI views to reflect the new state.
 */""")

replace_comment(
"""/**
 * @brief Searches the top-level open books model for an item matching the database node criteria.
 *
 * @param id The database row ID of the item.
 * @param type The generic schema type of the item.
 * @return The matching QStandardItem, or nullptr if not found.
 */""",
"""/**
 * @brief Utility facade to locate a physical UI tree node linked to a specific database record.
 *
 * A convenience wrapper around findItemRecursive. It initiates a full depth-first search
 * starting from the invisible root item of the primary openBooksModel.
 * Crucial for synchronizing UI selection states after backend database insertions or structural moves.
 *
 * @param id The primary key integer ID from the database record.
 * @param type The Qt::UserRole + 1 string type classifier (e.g., "document", "chat_node").
 * @return QStandardItem pointer if found, or nullptr if the node does not currently exist in the model.
 */""")

replace_comment(
"""/**
 * @brief Recursively traverses the standard item model tree looking for a specific node.
 *
 * @param parent The root of the sub-tree to search.
 * @param id The database row ID.
 * @param type The database schema type.
 * @return The matching QStandardItem, or nullptr if not found.
 */""",
"""/**
 * @brief Core recursive search engine for navigating QStandardItem tree hierarchies.
 *
 * Performs a deep dive through all nested children of the provided parent node. Matches are
 * verified using a composite key: the database ID (Qt::UserRole) AND the explicit string type
 * (Qt::UserRole + 1). This strict dual-check prevents cross-contamination (e.g., matching a
 * document with ID 5 to a chat_node with ID 5).
 *
 * @param parent The top-level QStandardItem pointer acting as the recursive search origin.
 * @param id The integer database ID to match.
 * @param type The string metadata type to match.
 * @return The matching QStandardItem pointer, or nullptr if the branch is exhausted without a match.
 */""")

replace_comment(
"""/**
 * @brief Constructs and displays the context menu for managing items inside the main tree view.
 *
 * @param pos The coordinate position to spawn the menu.
 */""",
"""/**
 * @brief Dynamically renders right-click context menus for the primary side-panel tree view (openBooksTree).
 *
 * Highly context-aware router function. It evaluates the exact node type clicked and generates
 * a bespoke QMenu on the fly:
 * - Root Books: Exposes close, debug, and global settings actions.
 * - Folders (Docs/Notes/Chats): Exposes 'New Item', 'Create Folder', and bulk Import actions.
 * - Chat Sessions: Exposes Export and Settings actions.
 * - Leaf Items (Documents/Notes): Exposes Export and explicit File Import/Replace actions.
 *
 * It intercepts the action triggered and routes to the appropriate modal dialogs or database transactions.
 *
 * @param pos The local QPoint mapped coordinates where the right-click occurred.
 */""")

with open("src/MainWindow.cpp", "w") as f:
    f.write(text)
