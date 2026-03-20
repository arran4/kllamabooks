If it is a `Qt::CopyAction` or `Qt::MoveAction`, we should handle it!
Currently:
```cpp
    if (draggedItem && moveItemToFolder(draggedItem, targetItem)) {
        dropEvent->acceptProposedAction();
        return true;
    }
```
`moveItemToFolder` only does MOVING!
Wait, the issue says: "file moves and copies should persist to database (basically we should be able to store the delta or rewrite the structure back to the database.)"
If I want to COPY a file to a new folder, I have to read the database item, and then `addDocument` / `addNote` / etc to the new folder!
BUT `moveItemToFolder` doesn't know what the action is! It just assumes move!
Wait, `openBooksTree->setDragDropMode(QAbstractItemView::DragDrop);` means it could be Copy!
Let's add a `bool isCopy` parameter to `moveItemToFolder`.
```cpp
bool MainWindow::moveItemToFolder(QStandardItem* draggedItem, QStandardItem* targetItem, bool isCopy) {
```
And then:
```cpp
    if (draggedItem && moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
```
In `moveItemToFolder`:
```cpp
    if (isCopy) {
        if (itemType == "document") {
            auto docs = db->getDocuments(targetFolderId); // wait, get from old id
            // Actually, we can just get from all docs
            auto all = db->getDocuments();
            for (const auto& d : all) if (d.id == itemId) db->addDocument(targetFolderId, "Copy of " + d.title, d.content);
            compatible = true;
        } else if (itemType == "template") {
            auto all = db->getTemplates();
            for (const auto& d : all) if (d.id == itemId) db->addTemplate(targetFolderId, "Copy of " + d.title, d.content);
            compatible = true;
        } else if (itemType == "draft") {
            auto all = db->getDrafts();
            for (const auto& d : all) if (d.id == itemId) db->addDraft(targetFolderId, "Copy of " + d.title, d.content);
            compatible = true;
        } else if (itemType == "note") {
            auto all = db->getNotes();
            for (const auto& d : all) if (d.id == itemId) db->addNote(targetFolderId, "Copy of " + d.title, d.content);
            compatible = true;
        } else if (itemType == "chat_session") {
            // Cannot easily copy a chat session with all its messages! Just ignore for now
        }
        if (compatible) {
            loadDocumentsAndNotes();
            return true;
        }
    } else {
        // ... existing move code ...
    }
```
Let's modify `MainWindow.h` and `MainWindow.cpp` to support `isCopy`!
