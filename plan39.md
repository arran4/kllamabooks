Wait, `moveItemToFolder` DOES call `loadDocumentsAndNotes()`.
```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        loadDocumentsAndNotes();  // This should be updated for the specific book if needed, but for now it's okay
        return true;
    }
```
If it calls `loadDocumentsAndNotes()`, the tree is REBUILT completely.
If the tree is rebuilt, `refreshVfsExplorer()` is NOT triggered!
Wait... `refreshVfsExplorer()` triggers on `rowsInserted` / `rowsRemoved`.
But `loadDocumentsAndNotes` calls `removeRows` and then `populateDocumentFolders` (which appends rows).
Why wouldn't it sync?
Because `loadDocumentsAndNotes` does:
```cpp
        if (docsFolder) {
            docsFolder->removeRows(0, docsFolder->rowCount());
            populateDocumentFolders(docsFolder, 0, "documents", db.get());
        }
```
When it does this, `rowsRemoved` and `rowsInserted` FIRE!
And `refreshVfsExplorer()` runs.
But wait! If the user dragged a file IN THE VFS EXPLORER, the user IS VIEWING the `docs_folder`!
So `refreshVfsExplorer()` is called!
Why does it not sync or persist?!

Wait... "I have 1 folder 'Folder' and 1 file 'File' in Documents. I drag and drop it using the list view file explorer... it doesn't persist, or update the tree."
If I drag "File" onto "Folder" in the VFS Explorer:
The item is dragged from the VFS Explorer, and dropped on the VFS Explorer.
Wait! `obj == vfsExplorer->viewport()` !!
In my previous patch, I added `vfsExplorer->viewport()->installEventFilter(this)`.
BUT in `QEvent::Drop` event filter:
```cpp
            } else if (targetView == vfsExplorer) {
                // Background drop in VFS explorer - move to current viewing folder
                QModelIndex treeIndex = openBooksTree->currentIndex();
                if (treeIndex.isValid()) targetItem = openBooksModel->itemFromIndex(treeIndex);
            }
```
Wait! Is `obj` equal to `vfsExplorer`?
If `obj` is the viewport, `targetView` will be `vfsExplorer`.
Let's check `eventFilter` logic:
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (dropEvent->source() == openBooksTree || dropEvent->source() == vfsExplorer)) {
            QAbstractItemView* targetView = (obj == openBooksTree || obj == openBooksTree->viewport()) ? openBooksTree : vfsExplorer;
```
This is correct.
```cpp
            if (targetIndex.isValid()) {
                if (targetView == openBooksTree) {
                    targetItem = openBooksModel->itemFromIndex(targetIndex);
                } else {
                    QStandardItem* vfsItem = vfsModel->itemFromIndex(targetIndex);
                    // ...
```
This is correct.

What is `dropEvent->source()`?
When dragging from `vfsExplorer`, what is `dropEvent->source()`?
Is it `vfsExplorer`? Or is it `vfsExplorer->viewport()`?
Ah! `dropEvent->source()` returns the WIDGET that initiated the drag!
If `QListView` initiates the drag, `source()` returns `vfsExplorer`!
Let's check the Qt docs or assume it's `vfsExplorer`.
If `dropEvent->source()` is `vfsExplorer`, then my code `(dropEvent->source() == openBooksTree || dropEvent->source() == vfsExplorer)` works!
BUT what if `dropEvent->source()` is `nullptr`? No, it's an internal drag.

Let's look at `targetIndex`.
```cpp
            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
```
Wait! `dropEvent->position().toPoint()` is in the coordinates of the VIEWPORT because `obj` is the viewport!
But `targetView->indexAt` expects coordinates in the viewport! Yes, `QAbstractScrollArea::indexAt` takes viewport coordinates!

Wait... If `moveItemToFolder` is returning TRUE, the tree updates. But the user says "it doesn't persist, or update the tree".
So `moveItemToFolder` is returning FALSE!
WHY would `moveItemToFolder` return `false`?
Let's print debug information mentally.
When dragging "File" onto "Folder" in VFS:
`targetIndex` is valid (it points to "Folder" in `vfsModel`).
`vfsItem` is "Folder".
`id` = Folder ID. `type` = "docs_folder" (since it's a folder in Documents).
`treeIndex` = `openBooksTree->currentIndex()`, which points to "Documents".
`book` is found correctly.
`targetItem = findItemRecursive(book, id, type)`.
Does `findItemRecursive` find "Folder"?
Yes! Its ID matches, and its type is "docs_folder".

Now `sourceIndex`:
`QModelIndex sourceIndex = sourceView->currentIndex();`
Wait! Is `sourceView->currentIndex()` the item being dragged?
When you click and drag in a `QListView`, does it select the item? Yes, clicking it selects it and makes it current.
But what if the user clicks and drags WITHOUT releasing the mouse button? Does `currentIndex` update on MousePress?
Yes, `QListView` updates the current index on MousePress!
So `sourceIndex` SHOULD be valid!
And it finds `draggedItem`.
Then it calls `moveItemToFolder(draggedItem, targetItem, false)`.

Wait. "When I do it from the tree it updates in the tree and for the file explorer."
If the user drags from TREE to TREE, it WORKS!
In TREE to TREE, `draggedItem` is `openBooksModel->itemFromIndex(sourceIndex)`.
`targetItem` is `openBooksModel->itemFromIndex(targetIndex)`.
`moveItemToFolder` returns `true`.

If it works in TREE, why doesn't it work in VFS?
Because when `targetItem` is extracted in VFS, something goes wrong!
Let's check the type of a folder in `vfsModel`.
In `refreshVfsExplorer`:
```cpp
    for (int i = 0; i < item->rowCount(); ++i) {
        QStandardItem* childTreeItem = item->child(i);
        QStandardItem* childItem = new QStandardItem(childTreeItem->icon(), childTreeItem->text());
        childItem->setData(childTreeItem->data(Qt::UserRole), Qt::UserRole);
        childItem->setData(childTreeItem->data(Qt::UserRole + 1), Qt::UserRole + 1);
        vfsModel->appendRow(childItem);
    }
```
So the `Qt::UserRole + 1` is correctly `"docs_folder"` for "Folder".

Wait... what if `targetItem = findItemRecursive(book, id, type);` FAILS?
Why would it fail?
Let's look at `findItemRecursive`:
```cpp
QStandardItem* MainWindow::findItemRecursive(QStandardItem* parent, int id, const QString& type) {
    if (parent->data(Qt::UserRole).toInt() == id && parent->data(Qt::UserRole + 1).toString() == type) {
        return parent;
    }
    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* found = findItemRecursive(parent->child(i), id, type);
        if (found) return found;
    }
    return nullptr;
}
```
Is `id` correct? Yes. Is `type` correct? Yes.
So `targetItem` should be found!

What about `draggedItem`?
```cpp
                    } else if (targetView == vfsExplorer) {
                        QStandardItem* vfsItem = vfsModel->itemFromIndex(sourceIndex);
```
Wait! My code says:
```cpp
                    } else if (dropEvent->source() == vfsExplorer) {
                        QStandardItem* vfsItem = vfsModel->itemFromIndex(sourceIndex);
                        if (vfsItem && vfsItem->text() != "..") {
```
Is `dropEvent->source()` EQUAL to `vfsExplorer`?
What if `dropEvent->source()` is `vfsExplorer->viewport()`?
Ah!!!!!!!
`dropEvent->source()` returns the WIDGET where the drag started!
If the drag started in the `vfsExplorer`, the widget is `vfsExplorer->viewport()` because that's where the mouse events are captured!
Wait, `QAbstractItemView::startDrag` creates a `QDrag` object.
`QDrag` takes a parent object. The parent object is usually the `QAbstractItemView` itself, NOT the viewport!
Let's check.
If `dropEvent->source() == vfsExplorer` was FALSE, then `draggedItem` would remain `nullptr`!
If `draggedItem` is `nullptr`, `moveItemToFolder` is NEVER called!
If it's never called, it falls through to:
```cpp
                    if (draggedItem && moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
So it calls `dropEvent->ignore()`!
If it calls `dropEvent->ignore()`, the native drop is CANCELED!
BUT WHY DOES THE USER SAY IT LOOKS LIKE IT WORKS?!
"I drag and drop it using the list view file explorer ... it doesn't persist, or update the tree."
If `dropEvent->ignore()` is called, the item should snap back! It should NOT visually move!
Wait, in `QListView`, if you move an item natively, it updates the model.
If I `return true` from `eventFilter`, the `QListView` NEVER GETS THE DROP EVENT!
So the `QListView` CANNOT NATIVELY MOVE IT!
If it cannot natively move it, how can it visually move?!
Ah! Is `obj` equal to `vfsExplorer` or `vfsExplorer->viewport()`?
If `eventFilter` catches the `Drop` event, it returns `true`.
If it returns `true`, the event is consumed!
Wait... "tree view drag drop works. However The drag drops I do inside the "central widget's file explorer list view" don't persist / apply but they look like they work can you please resolve that?"
This comment was BEFORE my `fix_dragdrop` and `fix_viewport` patches!
Wait, the user's FIRST comment was:
"tree view drag drop works. However The drag drops I do inside the "central widget's file explorer list view" don't persist / apply but they look like they work can you please resolve that?"
And then I pushed patches.
And then the user's NEW comment is:
"Actions in the File Explorer (central widget), such as drag drop move / copies don't seem to be replicated in the tree. The tree view is just fine."
And: "I have 1 folder 'Folder' and 1 file 'File' in Documents. I drag and drop it using the list view file explorer which si the central widget, it doesn't persist, or update the tree. When I do it from the tree it updates in the tree and for the file explorer. However neither changes persist when I restart the application"
Wait! The user says NEITHER changes persist when they restart!
Even the TREE drag drop doesn't persist when they restart!
WHY?!
If `db->moveItem` returns `true`, it executed `UPDATE documents SET folder_id = ? WHERE id = ?`.
Why would it NOT persist?
Is `m_filepath` being re-copied from somewhere?
No.
Is there an issue with `folder_id` in the database schema?
```cpp
            "CREATE TABLE IF NOT EXISTS documents ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "folder_id INTEGER DEFAULT 0, "
            "title TEXT, "
            "content TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
```
The column exists.
Is `id` correct?
Yes.
Why wouldn't it persist?
Wait! In `moveItemToFolder`:
```cpp
    QString targetType = targetItem->data(Qt::UserRole + 1).toString();
    int targetFolderId = targetItem->data(Qt::UserRole).toInt();

    if (!targetType.endsWith("_folder")) {
        if (targetItem->parent()) {
            targetItem = targetItem->parent();
            targetType = targetItem->data(Qt::UserRole + 1).toString();
            targetFolderId = targetItem->data(Qt::UserRole).toInt();
        } else {
            return false;
        }
    }
```
If `targetItem` is "Folder", its `targetType` is `"docs_folder"`.
`targetFolderId` is the ID of "Folder".
`db->moveItem("documents", itemId, targetFolderId)` is called.
It updates `folder_id = targetFolderId`.
When the application RESTARTS:
It calls `loadDocumentsAndNotes()`.
```cpp
void MainWindow::populateDocumentFolders(QStandardItem* parentItem, int folderId, const QString& type, BookDatabase* db) {
    // First, add subfolders
    QList<FolderNode> folders = db->getFolders(type);
    for (const auto& folder : folders) {
        if (folder.parentId == folderId) {
            // ...
            populateDocumentFolders(item, folder.id, type, db);
        }
    }

    // Then, add items in this folder
    if (type == "documents" || type == "templates" || type == "drafts") {
        QList<DocumentNode> docs;
        if (type == "documents") docs = db->getDocuments(folderId);
        // ...
```
If `targetFolderId` was correct, `db->getDocuments(targetFolderId)` SHOULD return the item!
Why does the user say "neither changes persist when I restart the application"?!
Wait... could `targetFolderId` be 0?
When they drag "File" onto "Folder", what is the ID of "Folder" in the tree?
When a folder is created:
```cpp
            int newFId = currentDb->addFolder(parentId, name, fType);
            QStandardItem* fItem = new QStandardItem(QIcon::fromTheme("folder-open"), name);
            fItem->setData(newFId, Qt::UserRole);
            fItem->setData(item->data(Qt::UserRole+1), Qt::UserRole+1);
```
Wait! `item->data(Qt::UserRole+1)` is the TYPE of the PARENT folder!
So `fItem` gets `targetType = "docs_folder"`.
And `targetFolderId = newFId`.
This is 100% correct!

Why doesn't it persist?
Is it possible the database ISN'T closed properly, so it doesn't flush?
No, SQLite flushes immediately upon `sqlite3_step` returning `SQLITE_DONE` unless in a transaction.
Is SQLCipher different? SQLCipher might require `sqlite3_close` to sync to disk? No, SQLCipher syncs to disk just like SQLite.
Wait! What if the user drags a folder into another folder?
```cpp
    else if (itemType.endsWith("_folder") && targetType == itemType) {
        if (db->moveFolder(itemId, targetFolderId)) {
```
This updates `folders` table.

Wait, is there any chance `db->moveItem` has a syntax error that causes it to fail, but it returns `true`?
```cpp
    QString sql = QString("UPDATE %1 SET folder_id = ? WHERE id = ?;").arg(safeTable);
```
No, that's valid SQLite.

Wait! I see it!
In `eventFilter` for `QEvent::Drop`:
```cpp
        if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (dropEvent->source() == openBooksTree || dropEvent->source() == vfsExplorer)) {
            // ...
            QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(dropEvent->source());
```
If `dropEvent->source()` is `openBooksTree->viewport()`, `qobject_cast<QAbstractItemView*>(dropEvent->source())` will be `nullptr`!
Because a viewport is a `QWidget`, not a `QAbstractItemView`!
If `sourceView` is `nullptr`, `sourceView->currentIndex()` crashes or is invalid!
Ah! `dropEvent->source()` returns the WIDGET that initiated the drag!
Is the widget `openBooksTree` or `openBooksTree->viewport()`?
If it's the viewport, `sourceView` becomes `nullptr`.
And `sourceIndex = sourceView->currentIndex()` causes a SEGFAULT if `sourceView` is null!
Wait, but the app didn't segfault, so `sourceView` is NOT `nullptr`.
So `dropEvent->source()` MUST be `openBooksTree` or `vfsExplorer`!
If it is, then `sourceIndex` is valid!

Let's re-read the user comment CAREFULLY.
"Actions in the File Explorer (central widget), such as drag drop move / copies don't seem to be replicated in the tree."
Wait. I added `refreshVfsExplorer()`. That refreshes the VFS.
But if you drop in the VFS, it doesn't update the tree?
Because `moveItemToFolder` is returning `false`!
WHY is `moveItemToFolder` returning `false` for VFS drops?
Let's add a debug statement, or find the flaw!
```cpp
                    if (dropEvent->source() == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (dropEvent->source() == vfsExplorer) {
```
Wait! What if `dropEvent->source()` is `vfsExplorer->viewport()`?
If it is `vfsExplorer->viewport()`, then BOTH `if` and `else if` fail!
`draggedItem` remains `nullptr`!
If `draggedItem` is `nullptr`, `moveItemToFolder` is skipped!
`dropEvent->ignore()` is called!
And the NATIVE `QListView` drop handles it anyway because I used `dropEvent->ignore()` and then `return true`? No, if `return true` is executed, the `QListView` does NOT receive the event!
BUT wait! I did:
```cpp
                    if (draggedItem && moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If `return true` is executed, it SHOULD block the native drop!
If it blocks the native drop, the visual move SHOULD NOT HAPPEN!
If the user says "it doesn't persist, or update the tree", it means the visual move HAPPENS!
If the visual move happens, then `return true` MUST NOT HAVE BEEN REACHED!
Or `QListView` handles it BEFORE `eventFilter` is called?
No, `eventFilter` is called first!
Wait! What if `obj` is `vfsExplorer` but the event is actually delivered to `vfsExplorer->viewport()`?
We added `vfsExplorer->viewport()->installEventFilter(this);`.
So `obj` is the viewport.
Then `obj == vfsExplorer || obj == vfsExplorer->viewport()` is TRUE!
So it enters the block!
But wait! `dropEvent->source()` is checked:
```cpp
                   (dropEvent->source() == openBooksTree || dropEvent->source() == vfsExplorer)
```
If `dropEvent->source()` is the viewport, then this condition is FALSE!
If the condition is FALSE, it falls through to:
```cpp
    return KXmlGuiWindow::eventFilter(obj, event);
```
Which PASSES THE EVENT TO THE QLISTVIEW!
AND THE QLISTVIEW NATIVELY MOVES IT!
AND OUR CODE IS NEVER EXECUTED!
YES!!! THIS IS IT!
`dropEvent->source()` is the VIEWPORT!
If it's the viewport, then `dropEvent->source() == vfsExplorer` is FALSE!
So our custom drop handler is completely bypassed for VFS-to-VFS drags!
And since it's bypassed, the native `QListView` moves the item visually, but NO DATABASE UPDATE happens, and the tree is NOT updated!
This PERFECTLY explains EVERYTHING!

Let's modify the condition to include the viewports!
```cpp
QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
    sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
}
```
If we do this, `sourceView` will resolve to `vfsExplorer` or `openBooksTree`!
Then we can check `(sourceView == openBooksTree || sourceView == vfsExplorer)`!
