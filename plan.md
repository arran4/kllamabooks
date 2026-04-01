1. **Add missing `deleteTemplate` to `BookDatabase`**:
   - Add `bool deleteTemplate(int id);` to `src/BookDatabase.h`.
   - Implement `deleteTemplate` in `src/BookDatabase.cpp` similar to `deleteDocument` but executing on the `templates` table.

2. **Modify Context Menu in `src/MainWindow.cpp` (`showItemContextMenu`)**:
   - Target the block handling `document`, `note`, `template`, `draft` types.
   - Expand the menu to match the tab actions requested by the user:
     - "Edit {Type}" (e.g. "Edit Document")
     - "History" (only for `document` type)
     - "AI Operations" (only for `document` type)
     - A "Danger" sub-menu containing a "Delete" action for all of these item types.
     - Add separators to organize.

3. **Implement Action Handling in `showItemContextMenu`**:
   - For all these actions, to reuse existing logic, we must ensure the `MainWindow` state is aware of the right-clicked item's ID. Wait, I can explicitly select the item first via `openBooksTree->setCurrentIndex(item->index())`. This automatically triggers `onOpenBooksSelectionChanged`, which sets `currentDocumentId`, `currentNoteId`, etc.
   - `editAction`: Select the item via `openBooksTree->setCurrentIndex(item->index())`. If it's a "document", additionally call the existing `onEditDocument()`.
   - `historyAction`: Select the item via `openBooksTree->setCurrentIndex`, then call `onDocumentHistory()`.
   - `aiAction`: Select the item via `openBooksTree->setCurrentIndex`, then call `onDocumentAIOperations()`.
   - `deleteAction`: Show a warning via `QMessageBox::question` confirming deletion. If confirmed:
     - Delete the record from the database calling the appropriate method (`deleteDocument`, `deleteDraft`, `deleteNote`, or `deleteTemplate`).
     - Refresh the UI by calling `loadDocumentsAndNotes()`.
     - (Optional but good) Switch the `mainContentStack` if necessary, or just rely on the selection change after deleting the item.

4. **Verify Changes**:
   - Use `git diff` to ensure `MainWindow.cpp` and `BookDatabase.h/cpp` were modified properly.
   - Run the build: `mkdir -p build && cd build && cmake .. && make`
   - Test if the project builds. Wait, I'll run `make` or `ninja` as configured in the repository. A proper verify step is `cmake --build build` or similar.

5. **Pre Commit**:
   - Complete pre-commit steps to ensure proper testing, verification, review, and reflection are done.
