So `loadDocumentsAndNotes()` IS being called!
If it is called, it re-queries the DB!
So the DB HAS the correct data!
So why doesn't it persist on restart?
Wait... what if `db->moveItem` is FAILING, but `moveItemToFolder` returns `true` anyway?
```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
```
If `db->moveItem` returns `false`, `moveItemToFolder` returns `false`.
If `moveItemToFolder` returns `false`, the tree DOES natively move it visually!
YES! `QTreeView` natively moves it visually if `moveItemToFolder` returns `false` AND `QEvent::Drop` doesn't explicitly `ignore()` it for the tree view!
In `eventFilter` for `QEvent::Drop`:
```cpp
        } else if ((obj == openBooksTree || obj == vfsExplorer) &&
                   (dropEvent->source() == openBooksTree || dropEvent->source() == vfsExplorer)) {
            // ...
                    if (draggedItem && moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If `moveItemToFolder` returns `false`, we call `dropEvent->ignore()` and `return true;`.
If we call `return true;`, `eventFilter` stops processing. The `QTreeView` NEVER receives the event!
So the `QTreeView` does NOT natively move it!
If `QTreeView` does NOT natively move it, and `moveItemToFolder` returned `false`, the tree DOES NOT visually move it!
BUT the user says: "When I do it from the tree it updates in the tree and for the file explorer. However neither changes persist when I restart the application"
This means the tree DOES update!
This means `moveItemToFolder` MUST be returning `true`!
And if it returns `true`, it MUST be calling `loadDocumentsAndNotes()`!
Wait! "I have 1 folder 'Folder' and 1 file 'File' in Documents. I drag and drop it using the list view file explorer which si the central widget, it doesn't persist, or update the tree. When I do it from the tree it updates in the tree and for the file explorer. However neither changes persist when I restart the application"

Wait, "I drag and drop it using the list view file explorer ... it doesn't persist, or update the tree."
If it doesn't update the tree, `moveItemToFolder` returned `false` for VFS drag/drop!
Why would it return `false` for VFS?
Because `itemType` or `targetType` is not matching!
In `moveItemToFolder`:
```cpp
    QString itemType = draggedItem->data(Qt::UserRole + 1).toString();
    int itemId = draggedItem->data(Qt::UserRole).toInt();
    QString targetType = targetItem->data(Qt::UserRole + 1).toString();
    int targetFolderId = targetItem->data(Qt::UserRole).toInt();
```
If I drag "File" onto "Folder", what is `itemType`? "document".
What is `targetType`? The type of "Folder" in VFS is "docs_folder"!
But `targetItem` from VFS DOES NOT HAVE `Qt::UserRole+1` set properly?
Wait! In `refreshVfsExplorer`:
```cpp
    for (int i = 0; i < item->rowCount(); ++i) {
        QStandardItem* childTreeItem = item->child(i);
        QStandardItem* childItem = new QStandardItem(childTreeItem->icon(), childTreeItem->text());
        childItem->setData(childTreeItem->data(Qt::UserRole), Qt::UserRole);
        childItem->setData(childTreeItem->data(Qt::UserRole + 1), Qt::UserRole + 1);
        vfsModel->appendRow(childItem);
    }
```
It DOES copy `Qt::UserRole + 1`!
Wait, but what if `targetItem` is the `..` folder?
```cpp
    if (type != "book") {
        QStandardItem* upItem = new QStandardItem(QIcon::fromTheme("go-up"), "..");
        vfsModel->appendRow(upItem);
    }
```
If `targetItem` is `..`, it has NO data set!
If we drop on `..`, `targetItem` from VFS has empty type!
BUT my previous patch resolved `..` to the parent folder in the TREE!
```cpp
                    if (vfsItem && vfsItem->text() == "..") {
                        QModelIndex treeIndex = openBooksTree->currentIndex();
                        if (treeIndex.isValid()) {
                            QStandardItem* currentFolder = openBooksModel->itemFromIndex(treeIndex);
                            if (currentFolder && currentFolder->parent()) targetItem = currentFolder->parent();
                        }
                    }
```
So `targetItem` becomes the parent folder from the tree! Which HAS a valid type!
So `moveItemToFolder` should see valid types.
But why does it return `false` for VFS?
"I drag and drop it using the list view file explorer which si the central widget, it doesn't persist, or update the tree."
If I drag a VFS item onto a VFS folder, `targetIndex` points to the VFS item.
```cpp
            if (targetIndex.isValid()) {
                if (obj == openBooksTree) {
                    targetItem = openBooksModel->itemFromIndex(targetIndex);
                } else {
                    QStandardItem* vfsItem = vfsModel->itemFromIndex(targetIndex);
                    if (vfsItem && vfsItem->text() == "..") {
                        // ...
                    } else if (vfsItem) {
                        int id = vfsItem->data(Qt::UserRole).toInt();
                        QString type = vfsItem->data(Qt::UserRole + 1).toString();
                        QModelIndex treeIndex = openBooksTree->currentIndex();
                        if (treeIndex.isValid()) {
                            QStandardItem* book = openBooksModel->itemFromIndex(treeIndex);
                            while (book && book->parent()) book = book->parent();
                            targetItem = findItemRecursive(book, id, type);
                        }
                    }
                }
            } else if (obj == vfsExplorer) {
                // Background drop in VFS explorer - move to current viewing folder
                QModelIndex treeIndex = openBooksTree->currentIndex();
                if (treeIndex.isValid()) targetItem = openBooksModel->itemFromIndex(treeIndex);
            }
```
Wait! Look at `targetItem = findItemRecursive(book, id, type);`!
If `targetItem` is found, it's the tree item.
Then:
```cpp
                    QStandardItem* draggedItem = nullptr;
                    if (dropEvent->source() == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (dropEvent->source() == vfsExplorer) {
                        QStandardItem* vfsItem = vfsModel->itemFromIndex(sourceIndex);
                        if (vfsItem && vfsItem->text() != "..") {
                            int id = vfsItem->data(Qt::UserRole).toInt();
                            QString type = vfsItem->data(Qt::UserRole + 1).toString();
                            QModelIndex treeIndex = openBooksTree->currentIndex();
                            if (treeIndex.isValid()) {
                                QStandardItem* currentBookOrFolder = openBooksModel->itemFromIndex(treeIndex);
                                QStandardItem* book = currentBookOrFolder;
                                while (book && book->parent()) book = book->parent();
                                draggedItem = findItemRecursive(book, id, type);
                            }
                        }
                    }
```
If BOTH `draggedItem` and `targetItem` are found in the tree, then `moveItemToFolder` is called with BOTH tree items.
And `moveItemToFolder` says:
```cpp
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
If they are both found from the SAME `book` via `findItemRecursive(book, ...)`, then `sourceBook` and `targetBook` MUST BE THE SAME BOOK!
So it does NOT return `false` here!
Then it checks types. `itemType` is "document". `targetType` is "docs_folder".
`compatible = true`.
It calls `db->moveItem("documents", itemId, targetFolderId)`.
This returns TRUE!
And it calls `loadDocumentsAndNotes()`.
And it returns TRUE!
So `dropEvent->acceptProposedAction()` is called!
And the tree IS updated!
BUT THE USER SAYS: "I drag and drop it using the list view file explorer ... it doesn't persist, OR UPDATE THE TREE."
WHY WOULD IT NOT UPDATE THE TREE IF I JUST PROVED IT RETURNS TRUE?!
Is there a flaw in my proof?
Wait! `sourceIndex` is `sourceView->currentIndex()`.
What if `sourceIndex` is NOT valid?
If you drag and drop without clicking first to select it, does `QListView` set `currentIndex` to the dragged item?
If `sourceIndex.isValid()` is false, `draggedItem` is `nullptr`!
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (sourceIndex.isValid()) {
                    // ...
```
If `sourceIndex` is not the item being dragged, then `draggedItem` is WRONG or `nullptr`!
Wait, `QDropEvent` doesn't provide the source index. You have to get it from `sourceView->currentIndex()`?
Actually, `sourceView->selectionModel()->selectedIndexes()` is better, because `currentIndex` might not be updated on drag start!
But even better, `vfsExplorer` is the source. If `currentIndex()` is valid, it uses it. If I select "File" and drag it, `currentIndex` IS valid!
Why would it NOT update the tree?
Wait! Look at the drag enter event!
```cpp
        if (dragEvent->source() == openBooksTree || dragEvent->source() == vfsExplorer) {
            dragEvent->acceptProposedAction();
            return true;
        }
```
Wait, `targetItem = findItemRecursive(book, id, type);`
If the folder was just created, `id` might be 0?
No, we proved earlier that creating a folder returns the correct ID from the DB.
Wait! What if the user drags "File" onto "Folder", and "Folder" is of type "docs_folder"?
```cpp
    bool compatible = false;
    QString table;
    if (itemType == "chat_session" && targetType == "chats_folder") { table = "messages"; compatible = true; }
    else if (itemType == "document" && targetType == "docs_folder") { table = "documents"; compatible = true; }
```
Wait! `itemType` is `"document"`. `targetType` is `"docs_folder"`.
`compatible` is `true`. `table` is `"documents"`.
BUT `db->moveItem(table, itemId, targetFolderId)` returns FALSE?
Why would `db->moveItem` return `false`?
Let's check `db->moveItem`:
```cpp
bool BookDatabase::moveItem(const QString& table, int id, int newFolderId) {
    if (!m_isOpen) return false;
    QString safeTable = table;
    if (table != "messages" && table != "documents" && table != "templates" && table != "drafts" && table != "notes") {
        return false;
    }
    QString sql = QString("UPDATE %1 SET folder_id = ? WHERE id = ?;").arg(safeTable);
    // ...
```
It returns `rc == SQLITE_DONE`, which is `true`.
Why would it NOT update the tree?
Could it be `obj == vfsExplorer` does NOT execute `targetItem = ...` because `eventFilter` is on `vfsExplorer->viewport()`?
Wait, when we installed the event filter:
```cpp
    vfsExplorer->installEventFilter(this);
```
Yes, the event filter is on `vfsExplorer`!
Is `targetIndex` valid when dropping on `vfsExplorer`?
`vfsExplorer->indexAt(dropEvent->position().toPoint())`
Wait, `QDropEvent::position().toPoint()` is in the widget's coordinates!
If the user drops it on the item, `index.isValid()` is `true`.
If the user drops it on the background, `index.isValid()` is `false`.
If it's `false`, `targetItem` becomes the current folder:
```cpp
            } else if (obj == vfsExplorer) {
                // Background drop in VFS explorer - move to current viewing folder
                QModelIndex treeIndex = openBooksTree->currentIndex();
                if (treeIndex.isValid()) targetItem = openBooksModel->itemFromIndex(treeIndex);
            }
```
If `targetItem` is the current folder (e.g. `docs_folder`), it is a `QStandardItem*`.
Then `moveItemToFolder` is called.
It should work!
Wait... "When I do it from the tree it updates in the tree and for the file explorer. However neither changes persist when I restart the application"
This means moving IN THE TREE works visually, but DOES NOT PERSIST!
Why would it NOT persist?!
If it works visually, `moveItemToFolder` returned `true`!
If `moveItemToFolder` returned `true`, it executed `db->moveItem("documents", itemId, targetFolderId)`!
If `db->moveItem` was executed, the SQLite database `UPDATE` query was run!
If the query was run and returned `SQLITE_DONE`, the database WAS MODIFIED!
Why doesn't it persist when restarting the application?!
Because SQLite transactions are not committed?!
SQLite auto-commits unless `BEGIN TRANSACTION` is used! We didn't use `BEGIN TRANSACTION` anywhere!
Wait, does `SQLITE_DONE` mean it matched a row?
NO! `SQLITE_DONE` just means the statement finished executing! If `id` did not match any row in the `documents` table, it STILL returns `SQLITE_DONE`!
Ah!!!!
If `id` did not match any row, it updates 0 rows! And returns `SQLITE_DONE`!
And `moveItemToFolder` returns `true`!
And `loadDocumentsAndNotes()` runs, but since no rows were updated in DB, the DB still has the old `folder_id`!
BUT WAIT! If `loadDocumentsAndNotes()` re-reads from the DB, and the DB has the OLD `folder_id`...
Then the tree SHOULD BE REBUILT WITH THE OLD FOLDER ID!
So the tree should REVERT IMMEDIATELY!
BUT the user says: "When I do it from the tree it updates in the tree and for the file explorer. However neither changes persist when I restart the application"
If it doesn't revert immediately, that means `loadDocumentsAndNotes()` is NOT being called?
Wait! In `moveItemToFolder`:
```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        loadDocumentsAndNotes(); // This should be updated for the specific book if needed, but for now it's okay
        return true;
    }
```
Wait! I changed `moveItemToFolder` earlier! Did I remove `loadDocumentsAndNotes()`?
Let's check the code for `moveItemToFolder`!
