Let's see why drag drop in the tree view isn't working.
In my previous patch, I changed it so we intercept ALL drops on the tree and VFS:
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
            QAbstractItemView* targetView = (obj == openBooksTree || obj == openBooksTree->viewport()) ? static_cast<QAbstractItemView*>(openBooksTree) : static_cast<QAbstractItemView*>(vfsExplorer);

            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
            QStandardItem* targetItem = nullptr;

            if (targetIndex.isValid()) {
                // ... gets targetItem
            } else if (targetView == vfsExplorer) {
                // Background drop in VFS explorer - move to current viewing folder
            }
```
If we drag a file and drop it on a folder in the tree view:
`targetIndex` IS valid.
`targetItem` is the folder.
Then it extracts `sourceIndex` and `draggedItem`.
Is `draggedItem` valid?
```cpp
            if (targetItem) {
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (sourceIndex.isValid()) {
                    QStandardItem* draggedItem = nullptr;
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (sourceView == vfsExplorer) {
                        // ...
                    }

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
If `moveItemToFolder` is returning `true`, the DB updates and `targetItem->appendRow(taken)` happens.
BUT what if it returns `false`?
Why would `moveItemToFolder` return `false`?
Let's trace `moveItemToFolder` for tree view drag drops!
Wait... "drag drop in the tree view isn't working"
Does the user mean "I drop it and it snaps back"?
If it snaps back, `dropEvent->ignore()` was called, meaning `moveItemToFolder` returned `false` OR `sourceIndex` was invalid OR `targetItem` was invalid.
Wait! What if the user drags multiple items? Or what if `sourceView->currentIndex()` is NOT the item being dragged?
I hypothesized earlier that `QTreeView` selects the item on MousePress, so `currentIndex()` should be valid.
But wait! If the user drops it BETWEEN items, `targetIndex.isValid()` is `false`!
If `targetIndex.isValid()` is `false`, `targetItem` is `nullptr`!
If `targetItem` is `nullptr`, we fall through to `dropEvent->ignore(); return true;`!
Is the user trying to drop it between items, expecting it to go into the parent folder?
In Qt, if you drag an item and drop it ON a folder, it goes inside.
If you drop it BETWEEN items, `indexAt` might fail!
BUT wait! If you drop it BETWEEN items in `QTreeView`, what is `targetIndex`?
`indexAt` only returns a valid index if the mouse is EXACTLY ON an item!
If it's between items, it returns invalid!
If it returns invalid, `targetItem` is `nullptr`!
Should we handle dropping between items?
If you drop between items in the tree, Qt native drop allows you to REORDER the items!
But our DB does not support reordering (there is no `position` column for files)!
Wait, if our DB does not support reordering, then dropping between items is technically invalid, OR it means "drop it into the same parent folder as the items you are between".
But if you drop it into the same parent folder, it's a no-op (it's already in that folder!).
So blocking it is fine.
But what if the user explicitly drops it ON the folder, and it still doesn't work?
Let's verify what `draggedItem` is.
If `draggedItem` is retrieved from `sourceView->currentIndex()`, is it correct?
Actually, `sourceView->selectionModel()->selectedIndexes()` is safer.
Wait, let's look at `QDropEvent`. Is there a way to extract the dropped items without `currentIndex`?
Qt `QStandardItemModel` encodes dropped items in `mimeData`. But decoding it is complex.
Wait! If `targetItem` is valid, and `draggedItem` is valid, `moveItemToFolder` is called.
Why would it fail?
```cpp
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
If we drag from tree to tree, `sourceBook` and `targetBook` should be the same.
Wait, what if `draggedItem` is the `book` item? (Dragging the root).
We shouldn't do that.
What if we drag "File" to "Folder"?
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
If I drag "File" onto another "File". `targetType` = "document".
`endsWith("_folder")` is FALSE.
`targetItem->parent()` is "docs_folder" (the parent folder).
`targetItem` becomes "docs_folder".
`targetType` becomes "docs_folder".
`targetFolderId` becomes the ID of the folder!
This means if you drop a file onto ANOTHER file, it moves it to the same folder as the target file!
And if you drop it onto a folder, it moves it into that folder!
So `targetItem` is ALWAYS resolved to a folder!

Wait! What if `db->moveItem` fails?
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
If it succeeds, it does `parent->takeRow(draggedItem->row())` and `targetItem->appendRow(taken)`.
Does this work?
Yes! `taken` is a list of items (usually 1 item, the file itself).
Then we append it to `targetItem`!
Wait! Does `targetItem->appendRow(taken)` visually move it?
Yes, it moves the item from one parent to another in the model.
So why does the user say "drag drop in the tree view isn't working"?
Did it crash? "isn't working" usually means it snaps back.
If it snaps back, it means `return false` from `moveItemToFolder` or `dropEvent->ignore()` was called.

Let's carefully read `eventFilter` for the Tree:
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
If `targetView == openBooksTree`, `targetItem` is `openBooksModel->itemFromIndex(targetIndex)`.
Is it possible `targetItem` is `nullptr`? No, if `targetIndex` is valid, `itemFromIndex` returns a valid pointer.
```cpp
            if (targetItem) {
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (sourceIndex.isValid()) {
                    QStandardItem* draggedItem = nullptr;
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (sourceView == vfsExplorer) {
```
Wait! What if `sourceIndex` is INVALID?
When dragging in `QTreeView`, you click and hold the mouse, and move it.
If the item was ALREADY selected, `currentIndex` is valid.
If the item was NOT selected, clicking it makes it selected AND current.
BUT wait! Does `QTreeView::indexAt(dropEvent->position())` work reliably?
Wait... what if `draggedItem` IS `nullptr` because `sourceIndex` is invalid?!
In `QAbstractItemView`, `currentIndex()` is NOT guaranteed to be the dragged item!
Especially if the drag was initiated, sometimes the drag system takes over before selection changes? No, selection changes on press.
Wait! What if `QTreeView` doesn't set `currentIndex()` on click, but on release?
No, it sets it on press.
But wait! What if `targetItem` is found, and `draggedItem` is found.
AND `moveItemToFolder(draggedItem, targetItem)` is called.
And it returns `false`!
WHY would it return `false`?
Could it be `draggedItem == targetItem`?
Yes, if you drag and drop on the same item, it returns `false`, which is correct.
Could `compatible` be `false`?
`itemType = draggedItem->data(Qt::UserRole + 1).toString();`
`targetType = targetItem->data(Qt::UserRole + 1).toString();`
If you drag a "document" onto a "docs_folder", `compatible` is `true`.
What if you drag a folder into a folder?
```cpp
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
Wait! If I drag a folder to a folder, `parent->takeRow(draggedItem->row())`.
Does `takeRow` take ALL children of the folder?
Yes, `QStandardItem` contains its children. Taking it takes the whole subtree!
So that's correct.

Wait... Look at `eventFilter`:
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport()) && dragEvent->source() == bookList) {
```
Wait! In `DragMove`, what do we do?
```cpp
    } else if (event->type() == QEvent::DragMove) {
        QDragMoveEvent* dragEvent = static_cast<QDragMoveEvent*>(event);

        QWidget* sourceWidget = qobject_cast<QWidget*>(dragEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }

        if (obj == openBooksTree || obj == openBooksTree->viewport()) {
            QModelIndex index = openBooksTree->indexAt(dragEvent->position().toPoint());
            if (index.isValid()) {
                QStandardItem* targetItem = openBooksModel->itemFromIndex(index);
                QString targetType = targetItem->data(Qt::UserRole + 1).toString();
                if (targetType.endsWith("_folder")) {
                    dragEvent->acceptProposedAction();
                    return true;
                } else {
                    dragEvent->ignore();
                    return true;
                }
            } else {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (obj == vfsExplorer || obj == vfsExplorer->viewport()) {
```
Wait!!!!
In `QEvent::DragMove` for `openBooksTree`!
```cpp
                QString targetType = targetItem->data(Qt::UserRole + 1).toString();
                if (targetType.endsWith("_folder")) {
                    dragEvent->acceptProposedAction();
                    return true;
                } else {
                    dragEvent->ignore();
                    return true;
                }
```
IF YOU DRAG OVER A FILE IN THE TREE, `targetType` is "document"!
"document" DOES NOT end with "_folder"!
So it executes `dragEvent->ignore(); return true;`!
If `DragMove` ignores the event over a file, you CANNOT drop it on a file!!!
And if you cannot drop it on a file, you CANNOT drop it between files, because if you hover over a file, it rejects it!
Wait! But in `moveItemToFolder`, I explicitly ADDED logic to handle dropping on a non-folder!
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
If `moveItemToFolder` SUPPORTS dropping on a file (it just moves it to the parent folder), WHY DID I REJECT IT IN `DragMove`?!
If I reject it in `DragMove`, the drop indicator NEVER shows up when hovering over a file!
And the user can NEVER drop it unless they hover exactly over a FOLDER!
And since they are dragging in the tree, they might be dragging a file and trying to drop it near other files in the same folder, but they hover over a file and the drop indicator turns into a "forbidden" sign!
YES!!!
This perfectly explains "drag drop in the tree view isn't working"! The user is probably trying to reorder or drop it onto the file list, and it shows the forbidden cursor!
Ah! In `vfsExplorer` I did the exact same thing:
```cpp
                if (targetType.endsWith("_folder") || targetItem->text() == "..") {
                    dragEvent->acceptProposedAction();
                    return true;
                } else {
                    dragEvent->ignore();
                    return true;
                }
```
So in VFS Explorer, you ALSO can't drop on a file!
But in VFS explorer, you can just drop on the background (empty space)!
```cpp
            } else {
                dragEvent->acceptProposedAction();
                return true;
            }
```
If you drop on empty space in VFS, it drops it into the current folder! So it works!
But in the tree view, there is almost NO empty space! The items fill the height! So you are almost always hovering over a file!
If you hover over a file, it rejects it!
So you can't drop it ANYWHERE in the tree unless you hover EXACTLY over a folder icon!
THIS IS IT!
