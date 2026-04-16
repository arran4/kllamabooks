We don't need ui testing or network based testing.
Never disable github workflows unless specified.

## Code Structure & Ordering

To maintain consistency and reduce merge conflicts, please follow this ordering for class members in `.cpp` files:

1.  **Includes** (grouped by library/module)
2.  **Constants / Static Helpers**
3.  **Constructor / Destructor**
4.  **Public Methods**
5.  **Slots** (grouped by functionality: Tray, List, Toolbar, etc.)
6.  **Private Helpers** (Setup, Logic)

In header files (`.h`), group declarations similarly and use comments to separate sections.

## Never Nest Principle

Avoid deep nesting of `if/else` blocks. Use guard clauses (early returns) to handle edge cases and error conditions first. This makes the "happy path" of the function less indented and easier to read.

## Function Size & Complexity

Break down large functions into smaller, single-purpose helper functions. This improves readability and makes the code easier to test and maintain.

## Building and Compiling (Qt6 / KF6 Migration)
This project is currently migrating to Qt6 and KDE Frameworks 6 (KF6).
If your environment does not have the required Qt6 or KF6 development headers, there is a Dockerfile located in `.jules/Dockerfile` that can be used to set up an isolated build container (e.g. `debian:testing`) with all required `libkf6*` dependencies.

## Application Architecture Learnings
- **Books & Databases:** The main application manages "Books" which represent individual encrypted SQLite databases (`BookDatabase`). These store chats, documents, and notes. The connection is handled using SQLCipher.
- **UI Components:** The main view utilizes `QSplitter`s. The left pane shows "Open Books" (tree view of databases and their contents) and "Closed Books" (list of inactive database files). The right pane is a `QStackedWidget` switching between a linear chat view (`QListWidget`) and a full branching chat tree (`QTreeView`).
- **Drag and Drop:** Handled in `MainWindow::eventFilter`. Dragging `.db` files from external sources or between "Open" and "Closed" lists is supported.
- **AI Interaction:** Chat messages support a forking feature (branching) starting from any node in the history. Individual messages track which LLM model generated them. All LLM interaction goes through `OllamaClient` using QtNetwork.
- **State Management:** Essential UI state (window geometry, splitter sizes, list of open books) is saved using `QSettings` in `MainWindow::closeEvent` and restored in `MainWindow::setupWindow`.
- Custom chat titles are dynamically resolved using `MainWindow::getChatNodeTitle` by traversing upward from the current leaf/fork node to find the latest inherited title in a linear path.
- Chat titles, user notes, and incomplete prompts (drafts) are persistently stored in the SQLite `settings` table using the `chat` scope and target ID of the corresponding message.
- **Markdown Document Editing:** Documents are edited using a dual-view approach (a `QStackedWidget` containing a `QTextEdit` for source editing and a `QTextBrowser` for Markdown preview).
- **Synchronous AI Operations:** For isolated, single-document text generation tasks (like "Complete this text" or "Replace entirely"), `OllamaClient::generate` is called directly from `MainWindow` rather than enqueuing a job in `QueueManager` (which is designed primarily for multi-turn chat message generation). These callbacks use concurrency checks (`m_generationId` and `currentDocumentId`) to prevent writing AI chunks to the wrong file if the user navigates away during generation.

- **Document Versioning:** Modifying AI documents via 'Replace in place' or 'Replace entirely' should default to creating a new nested sub-document (fork) representing the version history rather than destructive edits, mirroring the chat hierarchy. This requires the `BookDatabase` schema versions to track parent-child relationships for `documents`.
- **Note File System Support:** Dropping basic text-based files (`.txt`, `.log`, `.rtf`, `.md`) should integrate seamlessly into the native `Notes` folders to mimic free "file system" logic and upload support.
- **Robust Error Notifications:** For UI error handling around non-interactive components like single-document RAG generation callbacks or network failures, utilize internal application status bars rather than intrusive `QMessageBox` pop-ups unless the user is actively blocked within a guided workflow. Ensure missing local SQLite databases show a prompt to the user and don't crash.

- **Document AI & Queueing:** Document AI operations (e.g., text completion, rewriting) are fully integrated into the global `QueueManager` and are no longer dispatched synchronously in the UI thread. The SQLite `queue` table tracks a `target_type` (message vs document) to support this.
- **Configurable Prompts:** AI system prompts for document modes are user-configurable via `AiActionDialog`, saved via `QSettings`, and support the `{context}` placeholder variable for template injection.
- **Shortcuts:** The user prefers specific shortcut mappings: `Ctrl+Shift+N` must be mapped to 'New work book', `Ctrl+N` must dynamically create a 'New <Focused area>' item (e.g., new document or chat based on focus), and `Ctrl+Shift+M` is reserved for the 'Merge' action only when multiple documents are selected. Note: For KDE environments, `Ctrl+M` is often avoided due to conflicts with application menu bar toggles or ASCII carriage return interpretations. Use `Ctrl+Shift+M` or `Ctrl+Alt+M` for "Merge" shortcuts.
