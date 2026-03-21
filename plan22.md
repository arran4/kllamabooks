Let's analyze the new PR comments.
1. "tree view drag drop works. However The drag drops I do inside the "central widget's file explorer list view" don't persist / apply but they look like they work can you please resolve that?"
- In `vfsExplorer`, we fixed dropping *onto* `openBooksTree` from `vfsExplorer`, and `vfsExplorer` onto `vfsExplorer`. But wait...
In `vfsExplorer` dragging onto `vfsExplorer`, we set `sourceIndex` and `targetIndex`.
If `targetIndex` is invalid (dropping on empty space), `targetItem` becomes the current folder `openBooksTree->currentIndex()`.
Wait, does it call `loadDocumentsAndNotes()` and update `vfsExplorer`?
If `moveItemToFolder` returns true, it calls `loadDocumentsAndNotes()`.
But `loadDocumentsAndNotes()` rebuilds the tree items. It does NOT automatically rebuild the `vfsExplorer`!
In `onOpenBooksSelectionChanged`, `vfsModel` is cleared and populated.
But `loadDocumentsAndNotes()` does this:
```cpp
    if (currentIndex.isValid()) {
        QStandardItem* currentItem = openBooksModel->itemFromIndex(currentIndex);
        if (currentItem) {
            QString type = currentItem->data(Qt::UserRole + 1).toString();
            if (type == "docs_folder" || type == "templates_folder" || type == "drafts_folder" || type == "notes_folder" || type == "chats_folder") {
                emit openBooksTree->selectionModel()->selectionChanged(QItemSelection(currentIndex, currentIndex), QItemSelection());
            }
        }
    }
```
Wait! `loadDocumentsAndNotes` EMITS `selectionChanged`! This triggers `onOpenBooksSelectionChanged` which rebuilds `vfsModel`. So why does it "look like it works but doesn't persist"?
Wait! If I drag an item in `vfsExplorer` onto a folder in `vfsExplorer`:
```cpp
        } else if (obj == vfsExplorer) {
            QModelIndex index = vfsExplorer->indexAt(dragEvent->position().toPoint());
            if (index.isValid()) {
                QStandardItem* targetItem = vfsModel->itemFromIndex(index);
                QString targetType = targetItem->data(Qt::UserRole + 1).toString();
                if (targetType.endsWith("_folder") || targetType == "..") { // Wait, ".." is not a folder type, it's nothing!
```
Wait! In `moveItemToFolder`, `targetItem` is a `QStandardItem*`.
If `targetItem` comes from `vfsModel`, its `itemType` is right, but `targetItem->parent()` is NULL!
So `targetBook` resolves to `targetItem` (because no parent). And `targetBook->text()` is used to find the database!
BUT `targetItem` in `vfsModel` does NOT have a book text! Its text is just the folder name!
So `m_openDatabases.value(targetBook->text())` will return `nullptr`!
And `moveItemToFolder` returns `false`!
BUT `QListView` has `setDefaultDropAction(Qt::MoveAction)`. If it returns false, maybe `QListView` still moves the item visually because it's a drag-and-drop within the same model!
Yes! `vfsExplorer` is a `QListView` with `setDragDropMode(QAbstractItemView::DragDrop)`. It automatically moves the item in the model! But the database move fails because `targetBook` is wrong!
To fix: In `QEvent::Drop`, when calling `moveItemToFolder`, we MUST pass the REAL tree item for the target, just like we did for the source!
Ah!

2. "When the file system changs or a database is open the open/closed folders are lost these things should be remembered during the running of the application so that using the app isn't jarin"
Wait, "open/closed folders are lost" means expanded/collapsed state of the tree view!
When `loadDocumentsAndNotes` or `loadSession` runs, it clears the folders and re-adds them!
```cpp
        if (docsFolder) {
            docsFolder->removeRows(0, docsFolder->rowCount());
            populateDocumentFolders(docsFolder, 0, "documents", db.get());
        }
```
When it `removeRows`, the `QTreeView` loses the expanded state of all subfolders!
We need to save the expanded state before `loadDocumentsAndNotes` and restore it afterwards!
We can write a helper to save expanded state by ID/type, and restore it.

3. "When I'm in a chat fork and I start typeing in the chat this should open a "new chat" leaf it should be in draft state, at the right point too, the same as the other 2+ that are in the fork"
Wait, if I type in a chat, it currently does `discardChangesBtn->show()`.
The user wants it to immediately open a "new chat" leaf in draft state!
"it should be in draft state, at the right point too, the same as the other 2+ that are in the fork"
So if I am at node X and I type something in `inputField` or `multiLineInput`? No, "start typing in the chat" might mean typing in the chat history textarea? No, chat history is `setReadOnly(true)`.
Wait, "When I'm in a chat fork and I start typeing in the chat this should open a "new chat" leaf"
If they type in `inputField`? No, if they type in `inputField`, they are just preparing a message. It's not a fork until they send.
Ah! "start typing in the chat" = typing in the input box!
When they type in the input box, it should create a draft branch immediately?
"this should open a 'new chat' leaf it should be in draft state... the same as the other 2+ that are in the fork"
Wait, if it's a draft, it appears in the fork explorer!
So as soon as they type, we create a draft message?
Or does "start typing in the chat" mean editing the chat history?
"discardChangesBtn" appears when editing chat history! `chatTextArea` IS NOT read only if they change settings?
Wait, `chatTextArea` is `setReadOnly(true)` in `setupUi`. But wait!
```cpp
    chatTextArea->setReadOnly(true);
```
Is there a way to make it editable?
Ah! `discardChangesBtn` is connected to `chatTextArea->textChanged`:
```cpp
    connect(chatTextArea, &QTextEdit::textChanged, this, [this]() {
        if (!chatTextArea->isReadOnly() && !m_isGenerating && currentLastNodeId != 0) {
            discardChangesBtn->show();
        }
    });
```
How does it become not read-only? "Edit message" context menu? No, there is no such action.
Wait, "inputModeStack" has `multiLineInput` and `inputField`.
If they type in the input box, it creates a new chat leaf in draft state?
"When I'm in a chat fork and I start typeing in the chat this should open a "new chat" leaf it should be in draft state"
If they are at a fork (i.e. `currentLastNodeId` is a fork point), and they start typing a new prompt, they are branching! But wait, they only branch when they SEND!
Maybe they want a Draft to be created in the tree as soon as they start typing, so they can switch away and come back?
"When I navigate the app, and I type chat into one leaf, it should be saved as a draft at that point in the vfs and not be a pernament fixture of the chat, each chat should look lik eit's own"
Ah! The user wants the contents of `inputField` / `multiLineInput` to be saved as a draft associated with that chat node!
So if I type "Hello" in chat A, then click chat B, and come back to chat A, "Hello" should still be in the input field!
"saved as a draft at that point in the vfs and not be a permanent fixture of the chat"
If we save the input text for each `currentLastNodeId` in a map, or in the database!
Wait, if we save it in the database as a draft message?
"saved as a draft at that point in the vfs" -> so a Draft node in the VFS?
If I am at node X and type, it creates a node Y (Draft) as a child of X?

Let's read 5 and 6:
5. "Chat properties like the "model" and "multi line mode" should be moved to the status bar plus should be configuration options in the menu in chat for config / options (I believe it is)"
- "model" is currently in the status bar (`modelLabel`), but the selection button `modelSelectButton` is in the input layout. We should move `modelSelectButton` to the status bar? Or just make the status bar label clickable?
- "multi line mode" (`toggleInputModeBtn`) is in the input layout. Move to status bar?
- "plus should be configuration options in the menu in chat for config" - `inputSettingsButton` has a menu. We can add "Multi-line mode" to it.

6. "Model selection in status bar should tell you if it's that way because of a "global" "database" "chat" or "user switch" preference. (Listed from lowest precidence to highest)"
- When setting the model, we should track where it came from.

7. "Note saving, opening, etc is consistent and doesn't seem to be properly persisted."
- Note saving has a bug? `isCreatingNewNote` sets it to `noteEditorView->toPlainText()`. But opening a note: `mainContentStack->setCurrentWidget(noteContainer);` and sets text.
Wait, "Note saving, opening, etc is consistent and doesn't seem to be properly persisted." -> "is INconsistent and doesn't seem to be properly persisted"?
Let's check `saveNoteBtn`:
