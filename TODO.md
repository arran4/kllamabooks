# KLlamaBooks TODO List

## Core Architecture & Framework
- [x] Ensure main window uses `KXmlGuiWindow` (currently done, but needs verification of standard menus/toolbars setup).
- [x] Rename all remaining instances of `kgithub-notify` to `kllamabooks` across the entire project (e.g., `CMakeLists.txt`, `README.md`, source code).

## UI Layout & Navigation
- [x] Fix the `QSplitter` between the book list (left) and content area (right) to be resizable.
- [x] Set initial splitter sizes to `max(20%, 100px)` for the left pane and the remainder for the right pane.
- [x] Split the left pane into two sections (top ~60%, bottom ~40%):
  - [x] Top: "Open Books" (tree view showing opened books, chats, and documents).
  - [ ] Bottom: "Closed Books" (list of available databases, sortable by last opened, etc., with favorites support).
- [x] Add main application menus (File, Edit, View, Settings, Help) using KXMLGUI.
- [x] Add a status bar with relevant information (current model, connection status, etc.).

## Book Management (Databases)
- [x] Allow opening/closing books (moving them between the top and bottom lists).
- [x] Implement Drag & Drop for opening/closing books.
- [x] Add context menus for books (Open, Close, Delete, Properties).
- [x] Encryption Options: When creating/opening a book, provide a choice to save the password in KWallet or prompt for it every time.
- [x] Support marking books as favorites.

## Content Organization (Inside a Book)
- [x] Structure the "Open Books" tree to support folders and categorization:
  - `Book Name` -> `Chats` -> `[Chat Sessions]`
  - `Book Name` -> `Documents` -> `[Markdown Documents]`
  - `Book Name` -> `Notes` -> `[Plain Text Notes]`
- [x] Implement context menus for each item type (Rename, Delete, Export, etc.).

## Chat Interface & Mechanics
- [x] Update the chat view to appear linearly but support branching.
- [x] Implement "Forking" a chat:
  - When replying to a past message, create two branches from that point: the original path and the new path.
  - The UI should clearly show which branch is currently active and allow switching between them.
- [x] Allow selecting the LLM/Model for *each* individual response/message.
- [ ] Implement Import/Export functionality for chat sessions.
- [x] Add copy/paste support for messages.

## Document Management & AI Interaction (Non-Chat Modes)
- [x] Implement a basic document viewer/editor for Markdown files (`.md`) - Currently a basic QTextEdit.
- [ ] Add Document History Tracking / Forking (similar to chat).
- [ ] Add Drag & Drop support for `.md` files into and out of the application.
- [ ] Implement "Complete this text" / "Append" mode for documents.
- [ ] Implement "Replace entirely" mode for documents (with access to history).
- [ ] Implement "Replace in place" (modifying the current document directly, e.g., for censoring or minor edits).
- [ ] For replacement operations, default to creating a new sub-document (version history) rather than destructive edits.
- [ ] Design these interactions as fast, wizard-like dialogs rather than a conversational chat interface.
- [ ] Consider future RAG (Retrieval-Augmented Generation) capabilities (e.g., using documents as context for chats).

## Notes
- [x] Add a dedicated area/feature for user notes that are *explicitly excluded* from the LLM's context.
- [ ] Implement a free "file system" logic for notes with full CRUD capabilities and file upload support.

## Settings & LLM Management
- [x] Create a comprehensive Settings dialog.
- [x] Add an "LLM Management" section:
  - Add, edit, and remove different LLM providers/endpoints (e.g., local Ollama, remote servers).
  - Test connection to the configured endpoints.
- [x] Manage API keys or authentication if required for remote endpoints.
- [x] Show connection status and current endpoint in main toolbar.
- [ ] Implement a model explorer and downloader to browse available models and fetch them locally.

## Extra / Deduced Requirements
- [x] **Database Schema Updates:** The `BookDatabase` schema will need significant updates to support Documents, Notes, hierarchical folders, and the complex branching logic for chats (storing paths or explicit parent-child relationships).
- [ ] **State Persistence:** Save the state of open books, splitter sizes, and recent selections between application restarts.
- [ ] **Markdown Rendering:** Ensure the chat and document views can render Markdown properly (using `QTextBrowser` or a WebEngine view if needed).
- [ ] **Error Handling:** Robust error handling for database corruption, network failures with the LLM, and missing files.
- [ ] Implement Drag & Drop for opening/closing books.
