# KLlamaBooks TODO List

## Core Architecture & Framework
- [ ] Ensure main window uses `KXmlGuiWindow` (currently done, but needs verification of standard menus/toolbars setup).
- [ ] Rename all remaining instances of `kgithub-notify` to `kllamabooks` across the entire project (e.g., `CMakeLists.txt`, `README.md`, source code).

## UI Layout & Navigation
- [ ] Fix the `QSplitter` between the book list (left) and content area (right) to be resizable.
- [ ] Set initial splitter sizes to `max(20%, 100px)` for the left pane and the remainder for the right pane.
- [ ] Split the left pane into two sections (top ~60%, bottom ~40%):
  - [ ] Top: "Open Books" (tree view showing opened books, chats, and documents).
  - [ ] Bottom: "Closed Books" (list of available databases, sortable by last opened, etc., with favorites support).
- [ ] Add main application menus (File, Edit, View, Settings, Help) using KXMLGUI.
- [ ] Add a status bar with relevant information (current model, connection status, etc.).

## Book Management (Databases)
- [ ] Allow opening/closing books (moving them between the top and bottom lists).
- [ ] Implement Drag & Drop for opening/closing books.
- [ ] Add context menus for books (Open, Close, Delete, Properties).
- [ ] Encryption Options: When creating/opening a book, provide a choice to save the password in KWallet or prompt for it every time.
- [ ] Support marking books as favorites.

## Content Organization (Inside a Book)
- [ ] Structure the "Open Books" tree to support folders and categorization:
  - `Book Name` -> `Chats` -> `[Chat Sessions]`
  - `Book Name` -> `Documents` -> `[Markdown Documents]`
  - `Book Name` -> `Notes` -> `[Plain Text Notes]`
- [ ] Implement context menus for each item type (Rename, Delete, Export, etc.).

## Chat Interface & Mechanics
- [ ] Update the chat view to appear linearly but support branching.
- [ ] Implement "Forking" a chat:
  - When replying to a past message, create two branches from that point: the original path and the new path.
  - The UI should clearly show which branch is currently active and allow switching between them.
- [ ] Allow selecting the LLM/Model for *each* individual response/message.
- [ ] Implement Import/Export functionality for chat sessions.
- [ ] Add copy/paste support for messages.

## Document Management & AI Interaction (Non-Chat Modes)
- [ ] Implement a document viewer/editor for Markdown files (`.md`).
- [ ] Add Drag & Drop support for `.md` files into and out of the application.
- [ ] Implement "Complete this text" / "Append" mode for documents.
- [ ] Implement "Replace entirely" mode for documents (with access to history).
- [ ] Implement "Replace in place" (modifying the current document directly, e.g., for censoring or minor edits).
- [ ] For replacement operations, default to creating a new sub-document (version history) rather than destructive edits.
- [ ] Design these interactions as fast, wizard-like dialogs rather than a conversational chat interface.
- [ ] Consider future RAG (Retrieval-Augmented Generation) capabilities (e.g., using documents as context for chats).

## Notes
- [ ] Add a dedicated area/feature for user notes that are *explicitly excluded* from the LLM's context.

## Settings & LLM Management
- [ ] Create a comprehensive Settings dialog.
- [ ] Add an "LLM Management" section:
  - Add, edit, and remove different LLM providers/endpoints (e.g., local Ollama, remote servers).
  - Test connection to the configured endpoints.
- [ ] Manage API keys or authentication if required for remote endpoints.

## Extra / Deduced Requirements
- [ ] **Database Schema Updates:** The `BookDatabase` schema will need significant updates to support Documents, Notes, hierarchical folders, and the complex branching logic for chats (storing paths or explicit parent-child relationships).
- [ ] **State Persistence:** Save the state of open books, splitter sizes, and recent selections between application restarts.
- [ ] **Markdown Rendering:** Ensure the chat and document views can render Markdown properly (using `QTextBrowser` or a WebEngine view if needed).
- [ ] **Error Handling:** Robust error handling for database corruption, network failures with the LLM, and missing files.
