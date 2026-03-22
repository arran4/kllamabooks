Let's figure out why it doesn't do anything!
"when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view, can you please resolve these drag and drop issues?"
If it "doesn't do anything", that means `dropEvent->ignore()` was called!
Wait! If it snaps back and does NOTHING, my blocker is blocking it successfully, BUT `moveItemToFolder` is either not being called, OR returning `false`!

Let's trace:
Drag from VFS Explorer to Tree View.
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
            QAbstractItemView* targetView = (obj == openBooksTree || obj == openBooksTree->viewport()) ? static_cast<QAbstractItemView*>(openBooksTree) : static_cast<QAbstractItemView*>(vfsExplorer);

            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
            QStandardItem* targetItem = nullptr;

            if (targetIndex.isValid()) {
                if (targetView == openBooksTree) {
                    targetItem = openBooksModel->itemFromIndex(targetIndex);
                } else {
                    ...
```
`targetView` is `openBooksTree`.
`targetIndex` is valid (the folder in the tree).
`targetItem` is the folder in the tree.
```cpp
            if (targetItem) {
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (sourceIndex.isValid()) {
                    QStandardItem* draggedItem = nullptr;
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (sourceView == vfsExplorer) {
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
If `sourceView` is `vfsExplorer`:
`sourceIndex` is valid (the file in VFS).
It finds the `vfsItem`.
It finds `treeIndex = openBooksTree->currentIndex()`. This is valid (it's the folder we are currently viewing in VFS).
It traces up to the root to find `book`.
Then it does `draggedItem = findItemRecursive(book, id, type);`.
Wait! Is `draggedItem` found correctly? Yes.
Then:
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If `draggedItem` is valid, and `targetItem` is valid, it calls `moveItemToFolder`.
Why would `moveItemToFolder` fail?
```cpp
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
`sourceBook` and `targetBook` should be the same.
```cpp
    QString itemType = draggedItem->data(Qt::UserRole + 1).toString();
    int itemId = draggedItem->data(Qt::UserRole).toInt();
    QString targetType = targetItem->data(Qt::UserRole + 1).toString();
    int targetFolderId = targetItem->data(Qt::UserRole).toInt();

    if (draggedItem == targetItem) return false;

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
Wait! What if we drop a file onto a folder in the tree?
`targetType` is "docs_folder" or "notes_folder". It ends with "_folder".
```cpp
    bool compatible = false;
    QString table;
    if (itemType == "chat_session" && targetType == "chats_folder") {
        table = "messages";
        compatible = true;
    } else if (itemType == "document" && targetType == "docs_folder") {
        table = "documents";
        compatible = true;
    } else if (itemType == "template" && targetType == "templates_folder") {
        table = "templates";
        compatible = true;
    } else if (itemType == "draft" && targetType == "drafts_folder") {
        table = "drafts";
        compatible = true;
    } else if (itemType == "note" && targetType == "notes_folder") {
        table = "notes";
        compatible = true;
    } else if (itemType.endsWith("_folder") && targetType == itemType) {
        if (db->moveFolder(itemId, targetFolderId)) {
            QStandardItem* parent = draggedItem->parent();
            if (parent) {
                QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
                targetItem->appendRow(taken);
            }
            return true;
        }
    }
```
If `itemType` is "document" and `targetType` is "docs_folder", `compatible` = true.
```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
        return true;
    }
```
If `db->moveItem` returns `true`, it takes the row and appends it to `targetItem`.
Wait! "it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"

If it DOES NOTHING, maybe `moveItemToFolder` CRASHES? No, "doesn't do anything" means it snaps back.
Wait... what if `sourceIndex.isValid()` is FALSE?!
"When you click and drag an item in QTreeView, does it select the item?"
Yes, it selects it. So `sourceView->currentIndex()` SHOULD be valid.
But wait! What if `sourceView` is NOT what we think it is?!
In `eventFilter` for `QEvent::Drop`:
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }
```
Wait! I added this exact code to `QEvent::DragEnter` and `QEvent::Drop`!
BUT I DID NOT ADD IT TO `QEvent::DragMove`!
In `QEvent::DragMove`:
```cpp
    } else if (event->type() == QEvent::DragMove) {
        QDragMoveEvent* dragEvent = static_cast<QDragMoveEvent*>(event);
        if (obj == openBooksTree || obj == openBooksTree->viewport()) {
            dragEvent->acceptProposedAction();
            return true;
        } else if (obj == vfsExplorer || obj == vfsExplorer->viewport()) {
            dragEvent->acceptProposedAction();
            return true;
        }
```
In `DragMove`, it always accepts! So it should show the drop indicator.

If it shows the drop indicator, and you release the mouse, `QEvent::Drop` occurs!
In `QEvent::Drop`:
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }
```
If `sourceView` resolves to `openBooksTree`, then:
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (sourceIndex.isValid()) {
```
Is `currentIndex()` valid? Yes.
Then:
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If it returns `true`, it consumes the event.
If it consumes the event, DOES `QTreeView` natively move the item?!
NO!!!
If `eventFilter` returns `true`, the event is blocked from reaching the `QTreeView` completely!
Wait, but if I do `dropEvent->acceptProposedAction()`, doesn't Qt process it?!
NO! `eventFilter` returning `true` means the event is EATEN!
If the event is eaten, `QTreeView::dropEvent` is NEVER called!
If `QTreeView::dropEvent` is never called, the native Qt mechanism NEVER moves the item visually!
BUT WAIT!
If I ate the event, I MUST manually move the item visually!
And I DID add manual moving to `moveItemToFolder`:
```cpp
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
```
If I do `takeRow` and `appendRow`, the item is moved in the `QStandardItemModel`!
If the model is changed, the `QTreeView` MUST visually update!
Why doesn't it visually update?!
Wait!
"it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"
If the user says it doesn't do anything, maybe `moveItemToFolder` is returning `false`?!
WHY would it return `false`?
Could `draggedItem` be NULL?
Let's check `QDropEvent::source()`.
What if `dropEvent->source()` is NOT the viewport, but something else?
What if `sourceView` is NULL?
If `sourceView` is null, `if (sourceView == openBooksTree || sourceView == vfsExplorer)` is FALSE!
Then it falls through to:
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport()) && dropEvent->source() == bookList) {
```
And eventually it reaches:
```cpp
    return KXmlGuiWindow::eventFilter(obj, event);
```
If it reaches `return KXmlGuiWindow::eventFilter(obj, event);`, it returns `false`, and the NATIVE `QTreeView::dropEvent` RUNS!
If the native `QTreeView::dropEvent` runs, it SHOULD natively move the item!
BUT wait! My `fix_native_drop.patch` changed the end of the `if` block:
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
                }
            }
            dropEvent->ignore();
            return true;
```
If `sourceView` IS valid, it reaches the `if`.
If `sourceIndex` is invalid, or `targetItem` is null, it reaches `dropEvent->ignore(); return true;`!
If it reaches that, the native drop is BLOCKED!
And NOTHING happens!
Which perfectly matches "it doesn't do anything"!

So either `targetItem` is null, `sourceIndex` is invalid, OR `draggedItem` is null!
Why would `sourceIndex` be invalid?
When you click and drag in `QTreeView`, `currentIndex()` might NOT be what you think!
Wait! `QListView` and `QTreeView` implement drag and drop by selecting items.
If I use `sourceView->selectionModel()->selectedIndexes().first()`, it's safer.
```cpp
QModelIndex sourceIndex;
if (sourceView->selectionModel()->hasSelection()) {
    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
}
```
Wait! What if you select multiple items and drag them?
We only handle one item at a time.
What if `draggedItem` is failing?
If `sourceView == vfsExplorer`:
```cpp
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
```
Wait! `findItemRecursive` only searches descendants!
If `book` is the root book node, it searches the WHOLE book!
So it should find it!
What if `id` is 0?
When a file is just created, its `id` might be 0?
No, when a file is saved, it gets a non-zero ID. If it's unsaved, its ID is 0, but you shouldn't drag unsaved phantoms!
Wait... what if `type` is wrong?
`QString type = vfsItem->data(Qt::UserRole + 1).toString();`
Is this correct?
Yes, `populateDocumentFolders` sets `item->setData(type, Qt::UserRole + 1)`. Wait!
In `populateDocumentFolders`:
```cpp
            QString folderTypeSuffix = "_folder";
            if (type == "documents") item->setData("docs_folder", Qt::UserRole + 1);
```
So `type` is "docs_folder".
Wait! In `moveItemToFolder`:
```cpp
    QString itemType = draggedItem->data(Qt::UserRole + 1).toString();
```
If `itemType` is "docs_folder", it enters:
```cpp
    } else if (itemType.endsWith("_folder") && targetType == itemType) {
```
This is correct.

What if `moveItemToFolder` is failing because `db->moveItem` returns false?
Let's add some debugging code!
Actually, since I can't see the debug output because I'm not running the app interactively, let's use `statusBar->showMessage` to output what failed!
If I inject a `statusBar->showMessage("Failed to move: source or target invalid")`, I can trace it!
But I don't need to. Let's think.
What if `sourceView` is NULL?
If `sourceView` is NULL, it falls through to native drop. But native drop doesn't do anything because `dropEvent->ignore()` isn't reached! Wait, if `sourceView` is NULL, the outer `else if` fails!
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
```
If `sourceView` is NULL, this condition is FALSE.
If it's FALSE, it skips this block!
And returns `false` (native drop runs).
But the user says "it doesn't do anything".
If native drop runs, it SHOULD do something! (Move visually).
If it does NOTHING, then my block IS CATCHING IT!
Which means `sourceView` IS valid!
And it reaches `dropEvent->ignore(); return true;`!
Why does it reach `ignore()`?
Either:
1. `targetItem` is null.
2. `sourceIndex.isValid()` is false.
3. `draggedItem` is null.
4. `moveItemToFolder` returns `false`.

Let's look at `targetIndex`.
```cpp
            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
            QStandardItem* targetItem = nullptr;

            if (targetIndex.isValid()) {
                if (targetView == openBooksTree) {
                    targetItem = openBooksModel->itemFromIndex(targetIndex);
                } else {
                    ...
                }
            } else if (targetView == vfsExplorer) {
                QModelIndex treeIndex = openBooksTree->currentIndex();
                if (treeIndex.isValid()) targetItem = openBooksModel->itemFromIndex(treeIndex);
            }
```
If you drop between items in the tree view, `targetIndex` is INVALID!
If `targetIndex` is invalid, and `targetView` is `openBooksTree`, `targetItem` remains `nullptr`!
If `targetItem` is `nullptr`, it goes to:
```cpp
            if (targetItem) {
               ...
            }
            dropEvent->ignore();
            return true;
```
So it does NOTHING!
Which means the user is trying to drop BETWEEN items in the tree view!
If they drop BETWEEN items, `indexAt` returns invalid!
If it returns invalid, they are trying to reorder the item, OR move it to the current parent folder!
In a file browser tree, if you drop between items, you are dropping it into the PARENT folder of those items!
Wait! Does `QTreeView` support this natively?
Yes, if you drop between items, `dropEvent->parent()` gives you the parent!
Wait, `QDropEvent` doesn't have `parent()`. But `QTreeView` natively handles drops between items by checking the drop indicator position!
Since we block native drops, we must handle it!
Or, instead of blocking native drops for the tree, what if we JUST LET NATIVE DROPS HAPPEN, and THEN update the DB?!
How?
We can listen to `openBooksModel->rowsInserted` and `openBooksModel->rowsMoved`!
Wait, `QStandardItemModel` does NOT have `rowsMoved`. It implements drag and drop by `rowsInserted` and `rowsRemoved`!
If it does `rowsRemoved` then `rowsInserted`, we can't easily track where the item moved, because it's a completely new row with new items (although they might have the same UserData).
BUT catching the drag-and-drop is so much easier!

Wait! Is there a way to get the target index even if they drop between items?
If you drop between items, you want to drop it into the parent folder.
But wait! The user said: "when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"
The user explicitly said they drop it ON A FOLDER!
"drop it in a folder in the tree view" -> "onto a folder"
If they drop it ON a folder, `targetIndex.isValid()` is TRUE!
So `targetItem` is NOT null!
If `targetItem` is not null, then it must be `sourceIndex` or `draggedItem` or `moveItemToFolder` failing!

Let's check `sourceIndex`.
Is `sourceView->currentIndex()` valid?
If you click an item to drag it, it becomes current. So it should be valid.
But wait... if `sourceView` is `vfsExplorer` or `openBooksTree`, does `currentIndex()` return the dragged item?
Yes!
What if `draggedItem` is null?
```cpp
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (sourceView == vfsExplorer) {
```
If `sourceView` is `openBooksTree`, `draggedItem` is found perfectly.
So `draggedItem` is NOT null.

Then it must be `moveItemToFolder` returning `false`!
WHY would `moveItemToFolder` return `false`?
Let's check `moveItemToFolder` AGAIN!
```cpp
bool MainWindow::moveItemToFolder(QStandardItem* draggedItem, QStandardItem* targetItem, bool isCopy) {
    if (!draggedItem || !targetItem) return false;

    // Find root book items
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
Is `sourceBook` equal to `targetBook`?
Yes, if they are dragging within the same book.

```cpp
    QString itemType = draggedItem->data(Qt::UserRole + 1).toString();
    int itemId = draggedItem->data(Qt::UserRole).toInt();
    QString targetType = targetItem->data(Qt::UserRole + 1).toString();
    int targetFolderId = targetItem->data(Qt::UserRole).toInt();

    if (draggedItem == targetItem) return false;
```
Is `draggedItem == targetItem`? No, dragging "File" onto "Folder".

```cpp
    // Handle dropping on an item (non-folder)
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
If "Folder" is `docs_folder`, it skips this.

```cpp
    if (isCopy) {
        // ... (skipped if not copy)
    }
    bool compatible = false;
    QString table;
    if (itemType == "chat_session" && targetType == "chats_folder") {
        table = "messages";
        compatible = true;
    } else if (itemType == "document" && targetType == "docs_folder") {
        table = "documents";
        compatible = true;
    } else if (itemType == "template" && targetType == "templates_folder") {
        table = "templates";
        compatible = true;
    } else if (itemType == "draft" && targetType == "drafts_folder") {
        table = "drafts";
        compatible = true;
    } else if (itemType == "note" && targetType == "notes_folder") {
        table = "notes";
        compatible = true;
    } else if (itemType.endsWith("_folder") && targetType == itemType) {
        if (db->moveFolder(itemId, targetFolderId)) {
            QStandardItem* parent = draggedItem->parent();
            if (parent) {
                QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
                targetItem->appendRow(taken);
            }
            return true;
        }
    }
```
If `itemType` is "document", `targetType` is "docs_folder".
`table` = "documents". `compatible` = true.

```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
        return true;
    }
    return false;
```
If `db->moveItem` returns TRUE, it MUST do `appendRow`!
If `appendRow` happens, the tree visually updates!
But the user says "it doesn't do anything"!
If it doesn't do anything, it means `appendRow` did NOT happen!
If `appendRow` didn't happen, it means `db->moveItem` returned FALSE?!
Why would `db->moveItem` return `false`?!
```cpp
bool BookDatabase::moveItem(const QString& table, int id, int newFolderId) {
    if (!m_isOpen) return false;
    QString safeTable = table;
    if (table != "messages" && table != "documents" && table != "templates" && table != "drafts" && table != "notes") {
        return false;
    }
    QString sql = QString("UPDATE %1 SET folder_id = ? WHERE id = ?;").arg(safeTable);
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, newFolderId);
    sqlite3_bind_int(stmt, 2, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}
```
If `m_isOpen` is true, and the syntax is correct, `sqlite3_step` returns `SQLITE_DONE`!
It ALWAYS returns `SQLITE_DONE` if there are no constraints failing!
And `rc == SQLITE_DONE` is `true`.
If it returns `true`, then `compatible && db->moveItem(...)` is `true`!
And it DOES `appendRow`!
Why would the user say "it doesn't do anything"?!
Wait!
"when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
Look at `eventFilter` AGAIN.
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }

        if (dropEvent->mimeData()->hasUrls()) {
            // ...
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
```
If you drag from VFS, `sourceView` is `vfsExplorer`.
If you drop onto `openBooksTree` viewport:
`obj` is `openBooksTree->viewport()`.
`targetView` is `openBooksTree`.
`targetIndex = targetView->indexAt(dropEvent->position().toPoint());`
`targetItem = openBooksModel->itemFromIndex(targetIndex);`
If you dropped on a folder, `targetIndex` is valid.
`targetItem` is the folder.

Wait! What if `sourceIndex.isValid()` is FALSE?!
When you drag from VFS, `sourceView->currentIndex()` is checked.
Is `currentIndex()` valid during a drag-and-drop from `QListView`?
Actually, what if `dropEvent->source()` returns `vfsExplorer->viewport()`? Yes, it does.
Wait! `QListView` has `dragDropMode` set to `DragDrop`.
When you drag an item, it DOES select it.
But what if you drag from VFS to Tree?
Does the tree accept the proposed action?
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If `draggedItem` is NULL, it falls to `dropEvent->ignore()`.
Why would `draggedItem` be NULL?
```cpp
                    } else if (sourceView == vfsExplorer) {
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
Wait! What if `openBooksTree->currentIndex()` is INVALID when you drag from VFS?!
If you are dragging from VFS, the VFS shows the contents of `openBooksTree->currentIndex()`. So it MUST be valid!
But wait! When you drag over the tree, your mouse is hovering over OTHER folders!
If you hover over other folders, does the `QTreeView` update `currentIndex()`?!
NO! It only updates the drop target!
But wait! `QTreeView` DOES NOT change `currentIndex()` during a drag!
So `openBooksTree->currentIndex()` SHOULD STILL BE the folder you are viewing in VFS!
So `draggedItem = findItemRecursive(book, id, type)` SHOULD work!

What if `sourceIndex` is NOT valid?
What if `sourceView->selectionModel()->selectedIndexes().first()` is better?
`QModelIndexList selected = sourceView->selectionModel()->selectedIndexes();`
`QModelIndex sourceIndex = selected.isEmpty() ? QModelIndex() : selected.first();`
This is MUCH safer! `currentIndex()` might get cleared or changed!
Let's change ALL `currentIndex()` to `selectedIndexes().first()` for drag sources!

Wait, is there ANY OTHER REASON `moveItemToFolder` would return false?
What if `itemType` is empty?
No, we verified it's populated.
What if `targetType` is "docs_folder" and `itemType` is "document", but `targetFolderId` is 0?
Wait! In `moveItemToFolder`:
```cpp
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
If you drag "File" onto "Folder", `targetType` is "docs_folder". `targetType.endsWith("_folder")` is TRUE.
It skips this `if`!
Wait! "If you drag 'File' onto 'Folder' in the tree view it doesn't do anything."
Wait! If `targetType` is "docs_folder", it enters `compatible && db->moveItem`!
And it returns `true`!

Wait... I just saw something!
```cpp
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
        return true;
```
If `draggedItem` is a node in the tree...
Does `parent` exist?
Yes, "File" is inside "Documents". So `parent` is the "Documents" folder!
If `parent->takeRow` removes it, and `targetItem->appendRow` adds it...
IT SHOULD WORK!

Wait... "when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"
If BOTH do nothing, it means `QDropEvent` is NOT accepted!
Why?
Maybe `dropEvent->dropAction()` is NOT `Qt::MoveAction` or `Qt::CopyAction`?
If the user holds Ctrl, it's Copy. Otherwise it's Move.
Wait... in `QTreeView`, default drop action is `MoveAction`.
What if `eventFilter` is NOT returning `true`?
We have:
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
It returns `true` ALWAYS!
So the event is ALWAYS swallowed!
If the event is swallowed, `QTreeView` NEVER receives it.
If `QTreeView` never receives it, the ONLY visual update is `targetItem->appendRow(taken);`.
Wait! If `targetItem->appendRow(taken);` happens...
Does the UI update IMMEDIATELY?
Yes, `QStandardItemModel` signals the view.
But if the user says "it doesn't do anything", it means `targetItem->appendRow(taken)` DID NOT HAPPEN!
Which means `moveItemToFolder` returned `false`!
Or `draggedItem` is NULL!
Or `targetItem` is NULL!

Why would `targetItem` be NULL?
If `targetIndex` is invalid.
Is it possible `targetIndex` is invalid because `dropEvent->position().toPoint()` is WRONG?!
In my previous patch, I changed `dropEvent->pos()` to `dropEvent->position().toPoint()`.
Does `dropEvent->position().toPoint()` return the position relative to the viewport?
Yes, for `QDropEvent`, `position()` is in the coordinates of the widget receiving the event!
If the widget is `openBooksTree->viewport()`, the coordinates ARE relative to the viewport!
And `openBooksTree->indexAt()` EXPECTS coordinates relative to the viewport!
So `indexAt` SHOULD WORK!

Wait, is it possible that `targetItem` and `draggedItem` are the SAME item?!
If you drop "File" onto "File", `moveItemToFolder` resolves `targetItem` to the PARENT folder.
Then `draggedItem == targetItem` is checked!
"File" == "Folder" ? NO!
So it proceeds.

Let's rethink `draggedItem`.
If `sourceIndex.isValid()` is true, and it finds `draggedItem`.
What if `sourceIndex` is WRONG?
If `sourceView->currentIndex()` is NOT the dragged item!
If I click and drag a file in the tree, does `QTreeView` change `currentIndex`?
YES!
But what if the drop happens, and `currentIndex()` is somehow cleared?
Let's use `selectedIndexes().first()`!

What if `db->moveItem` IS returning `false`?!
Why would it?
"1 folder 'Folder' and 1 file 'File' in Documents"
`itemType = "document"`. `targetType = "docs_folder"`.
`compatible = true`. `table = "documents"`.
`db->moveItem("documents", itemId, targetFolderId)`.
What if `itemId` is 0?
Wait... when is the ID assigned?
When you click "New Document", it adds a phantom item with ID 0.
Then you type, and the auto-draft timer fires, and it creates a DRAFT with a new ID!
Wait! It creates a DRAFT with a new ID!
But the TREE ITEM still has ID 0?!
Wait, in my auto-draft timer:
```cpp
        if (currentAutoDraftId == 0) {
            currentAutoDraftId = currentDb->addDraft(0, title, textToSave);
        }
```
Does it update the TREE ITEM's ID?!
NO! The tree item for the document still has ID 0!
It only updates the tree item's ID when you click SAVE!
```cpp
            item->setData(newId, Qt::UserRole);
            item->setData(type, Qt::UserRole + 1);
            item->setText(currentTitle);
```
If you DID NOT click SAVE, the document has ID 0!
If the document has ID 0, `db->moveItem` executes `UPDATE documents SET folder_id = ? WHERE id = 0`!
This matches 0 rows! `sqlite3_step` returns `SQLITE_DONE` (which is `true`)!
And `moveItemToFolder` returns `true`!
And it does `targetItem->appendRow(taken)`!
So it SHOULD move visually!
Wait, if it moves visually, the user wouldn't say "it doesn't do anything"!
They would say "it moves but doesn't persist".
But they said "it doesn't do anything"!

Wait... what if `sourceBook` and `targetBook` are DIFFERENT?
```cpp
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
If you drag "File" onto "Folder", what is `sourceBook`?
"File" -> "Documents" -> "My Book".
`sourceBook` is "My Book".
What is `targetBook`?
"Folder" -> "Documents" -> "My Book".
`targetBook` is "My Book".
They ARE the same!

Could it be that the user is trying to drag and drop multiple files? No, "a file".
Could it be `sourceView` logic?
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }
```
If I drag from the tree, `dropEvent->source()` is `openBooksTree`.
`sourceWidget` = `openBooksTree`.
`qobject_cast<QAbstractItemView*>(openBooksTree)` is VALID! So `sourceView` = `openBooksTree`!
This is perfect.

Wait! What if my `eventFilter` is completely bypassing the NATIVE drops, but my manual `appendRow` is throwing an error or not behaving right?
If `draggedItem->row()` is taken, and appended to `targetItem`...
Wait! `parent->takeRow` removes the row and returns a list of items (the row elements).
If I append it to `targetItem`, it moves it!
But wait! If I drag it in VFS Explorer, `targetItem` is a tree folder. It appends it to the tree folder!
If it appends it to the tree folder, the VFS explorer doesn't update unless `refreshVfsExplorer()` is called!
Does `refreshVfsExplorer()` get called?
Yes, `rowsRemoved` and `rowsInserted` on `openBooksModel` trigger `refreshVfsExplorer()`!
So the VFS explorer SHOULD update!
Why doesn't it do anything?!

Wait! Look at this carefully:
```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
        return true;
    }
```
When we `takeRow`, the item is removed from `openBooksModel`.
When we `appendRow`, it is added to `targetItem` in `openBooksModel`.
But wait! Does `takeRow` trigger `rowsRemoved`? Yes!
Does `appendRow` trigger `rowsInserted`? Yes!
But wait... if `refreshVfsExplorer()` is called, it clears the `vfsModel` and recreates it.
If the VFS model is cleared and recreated WHILE processing the drop event...
The VFS explorer is the TARGET of the drop event!
If we clear the VFS model INSIDE the drop event handler of the VFS explorer...
Does it crash?
If it crashed, the user would say "it crashes".
But they said "it doesn't do anything".
Maybe `QListView` aborts the drop if the model resets during it?
But we returned `true`! The `QListView` doesn't even process the drop!
So the event is EATEN.

Wait... if the user says "it doesn't do anything", could it be that my drop blocker in `DragMove` is still blocking it?
Let's check my `DragMove` handler again:
