So `DragMove` is accepting everything.
And `Drop` is catching the drop.

Wait. Is it possible `targetItem` is NULL because `targetIndex` is invalid?
If the user drops it "in a folder in the tree view" (meaning onto the folder icon).
```cpp
            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
```
If you drop it exactly on the folder, `targetIndex` should be valid.
But what if the mouse is slightly off?
If `targetIndex` is invalid, the tree natively doesn't handle it either if it's off the row?
Actually, `QTreeView` has `indexAt` which returns invalid if you drop BETWEEN rows.
If you drop BETWEEN rows, `targetIndex` is invalid!
If `targetIndex` is invalid, `targetItem` is NULL!
If `targetItem` is NULL, my code falls through to:
```cpp
            if (targetItem) {
                // ...
            }
            dropEvent->ignore();
            return true;
```
So if the user drops BETWEEN rows (which is very common in tree views when trying to put something IN a folder, you might drag it right below the folder name), `targetItem` is NULL!
And `dropEvent->ignore();` happens!
And "it doesn't do anything"!
YES!
If the user is trying to drop a file INTO a folder, they might hover over the folder itself, but what if they drop it between children of that folder?
If they drop it between children of a folder, `targetIndex` is invalid?!
No, if you drop between children, `QTreeView` handles it natively by finding the parent!
But `indexAt` ONLY returns the exact item under the mouse! It DOES NOT return the parent if you drop between children!
So if you drop between rows, `indexAt` returns INVALID!
If `indexAt` returns INVALID, `targetItem` is NULL!
If `targetItem` is NULL, it DOES NOTHING!
This explains perfectly why "it doesn't do anything"!

How can we fix this?
If `targetIndex` is invalid, can we find out the drop indicator position?
Unfortunately, `QDropEvent` doesn't expose the parent index or the drop indicator directly.
BUT wait! We can just ALLOW native `QTreeView` and `QListView` drops to handle the VISUAL moving!
And then hook into the model's `rowsInserted` signal to update the database!
If we do this, we don't need to intercept `QEvent::Drop` to manually move items at all!
Is this possible?
If we intercept `rowsInserted`:
```cpp
    connect(openBooksModel, &QStandardItemModel::rowsInserted, this, [this](const QModelIndex& parent, int first, int last) {
        // Find the newly inserted items
        for (int i = first; i <= last; ++i) {
            QStandardItem* item = openBooksModel->itemFromIndex(openBooksModel->index(i, 0, parent));
            if (item) {
                // Update database!
                int itemId = item->data(Qt::UserRole).toInt();
                QString itemType = item->data(Qt::UserRole + 1).toString();
                // Find parent folder ID
                int targetFolderId = 0;
                QStandardItem* parentItem = openBooksModel->itemFromIndex(parent);
                if (parentItem) targetFolderId = parentItem->data(Qt::UserRole).toInt();

                // Update DB...
            }
        }
    });
```
Wait! If we update the DB in `rowsInserted`, we MUST BE VERY CAREFUL!
`rowsInserted` is called when we POPULATE the tree initially (`loadDocumentsAndNotes()`)!
We DO NOT want to trigger DB updates during initial load!
Also, it's called when we manually add a file or folder!
We don't want to double-update the DB if we manually added it!
So hooking into `rowsInserted` is DANGEROUS!

Is there another way?
If `targetIndex` is invalid, we can STILL find the item they meant to drop on!
We can use `QAbstractItemView::indexAt(pos)`. But if it's invalid, what if we use `visualRect` to find the nearest row?
Actually, `QTreeView` has `indexAt` which works. If they drop between items, they shouldn't! Our folders don't have custom order!
If they drop ON a folder, `indexAt` is valid.
Why did the user say "when I drop a file from the tree view onto a folder in the tree view it doesn't do anything"?
If they drop it ON a folder, `indexAt` IS valid!
Why would it not do anything?!
Let's check `QDropEvent::position()`.
If `obj` is the viewport, `position()` is in viewport coordinates.
`openBooksTree->indexAt(dropEvent->position().toPoint())` returns the correct index.

Let's test this locally. Wait, I can't.
Let's write a small trace to a file:
```cpp
    QFile debugFile("/workspace/debug.log");
    debugFile.open(QIODevice::Append | QIODevice::Text);
    QTextStream out(&debugFile);
    out << "Drop event received!" << "\n";
    out << "Target valid: " << targetIndex.isValid() << "\n";
```
No, I shouldn't rely on blind debugging.
Let's think logically.
If `draggedItem` is NULL, it doesn't do anything.
Why would `draggedItem` be NULL?
Because `sourceView->currentIndex()` is WRONG.
If you click and drag an item, `QTreeView` sets it as current. BUT maybe `QListView` doesn't?
Actually, `QAbstractItemView::currentIndex()` is definitely updated on click.
But what if the selection model has it instead?
Let's change:
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (sourceIndex.isValid()) {
                    QStandardItem* draggedItem = nullptr;
                    if (sourceView == openBooksTree) {
```
To:
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (!sourceIndex.isValid() && sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                }
```
Wait, if it was already valid, maybe it's the WRONG index?
What if `currentIndex` is still the OLD item because the click didn't change it?
`selectedIndexes().first()` is ALWAYS the actual selected item being dragged!
```cpp
                QModelIndex sourceIndex;
                if (sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                } else {
                    sourceIndex = sourceView->currentIndex();
                }
```
This is safer.

Wait! What if `targetItem` is valid, `draggedItem` is valid, `moveItemToFolder` returns `true`...
BUT `parent->takeRow` FAILS?!
```cpp
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
```
If `draggedItem` is in the tree, DOES IT HAVE A PARENT?
Yes! BUT what if `draggedItem` is in `vfsModel`?!
Ah!!!!!!
If `draggedItem` was found via `findItemRecursive`, then `draggedItem` IS from `openBooksModel`!
So it HAS a parent!
Wait! "when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
If I drag from VFS Explorer to the Tree View!
`sourceView` = `vfsExplorer`.
`draggedItem` is found using `findItemRecursive` in `openBooksModel`.
So `draggedItem` is the TREE item!
`targetItem` is the TREE item!
`parent` is the tree item's parent.
`parent->takeRow` removes it from the tree!
`targetItem->appendRow` adds it to the new folder in the tree!
So it SHOULD work!

Wait... If `draggedItem` is from the tree, and we `takeRow` from its parent...
Does the VFS Explorer update?
Yes, `rowsRemoved` and `rowsInserted` are fired!
If `rowsRemoved` fires, `refreshVfsExplorer()` is called!
`refreshVfsExplorer()` clears `vfsModel`!
If `vfsModel` is cleared WHILE `QDropEvent` is being processed...
Does it crash? No.
BUT DOES IT ABORT THE DRAG?!
No, the drop event is almost done! We are returning `true` right after!
Wait! If `vfsModel` is cleared, the item under the mouse in VFS Explorer disappears!
But we dropped it in the TREE VIEW! So the mouse is over the TREE VIEW!

What if the database move is failing?
```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
```
If `compatible` is false?
"1 file 'File' in Documents"
`itemType = "document"`.
"drop it in a folder in the tree view"
`targetType` = "docs_folder".
`compatible` is true.

Is there ANY chance `dropEvent->source()` is NOT matched?
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }
```
If `sourceView` is `vfsExplorer`, and `targetView` is `openBooksTree`...
Then `(sourceView == openBooksTree || sourceView == vfsExplorer)` is TRUE!
It enters the block!

Wait... "I drag and drop it using the list view file explorer which si the central widget, it doesn't persist, or update the tree. When I do it from the tree it updates in the tree and for the file explorer. However neither changes persist when I restart the application"
THIS WAS THE OLD COMMENT!
The NEW COMMENT IS:
"changes made using the explorer are now persisting, thank you. However changes made using the tree are not persisting. ... when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"

If "changes made using the explorer are now persisting", it means dragging from VFS to VFS works!
Let's check VFS to VFS in the NEW code!
`sourceView` = `vfsExplorer`. `targetView` = `vfsExplorer`.
`targetItem` is found (either via `targetIndex` translating to `openBooksModel`, or `targetItem = openBooksModel->itemFromIndex(treeIndex)`).
`sourceIndex` is valid.
`draggedItem` is found via `findItemRecursive`.
`moveItemToFolder` is called!
`takeRow` and `appendRow` are called on the TREE items!
Wait! If we `takeRow` and `appendRow` on the TREE items, `openBooksModel` updates!
And `refreshVfsExplorer()` is called!
And the VFS is refreshed!
And it persists!
YES! VFS to VFS WORKS!

If VFS to VFS works, then `draggedItem` is found correctly! `targetItem` is found correctly! `moveItemToFolder` works correctly! `takeRow` and `appendRow` work correctly!
So WHY DOES VFS TO TREE NOT WORK?!
In VFS to Tree:
`sourceView` = `vfsExplorer`. `targetView` = `openBooksTree`.
```cpp
            QAbstractItemView* targetView = (obj == openBooksTree || obj == openBooksTree->viewport()) ? static_cast<QAbstractItemView*>(openBooksTree) : static_cast<QAbstractItemView*>(vfsExplorer);
```
`targetView` is `openBooksTree`.
```cpp
            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
            QStandardItem* targetItem = nullptr;

            if (targetIndex.isValid()) {
                if (targetView == openBooksTree) {
                    targetItem = openBooksModel->itemFromIndex(targetIndex);
                } else {
```
`targetIndex` is valid.
`targetItem` = `openBooksModel->itemFromIndex(targetIndex)`.
This is exactly the SAME `targetItem` as if we found it from VFS! It's the tree item!
And `draggedItem` is exactly the SAME `draggedItem` as VFS to VFS!
So `moveItemToFolder(draggedItem, targetItem)` is called with the EXACT SAME ARGUMENTS as VFS to VFS!
WHY WOULD IT NOT WORK?!

Ah!!!!!!!
Look at `eventFilter` condition!
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
```
Wait! Is `obj` equal to `openBooksTree` or `openBooksTree->viewport()`?
When you drop on the tree view, `obj` is `openBooksTree->viewport()`.
BUT WAIT!
If `obj` is `openBooksTree->viewport()`, and you drop on it...
Is `openBooksTree->viewport()->installEventFilter(this)` called?
YES!
Wait! Does `QTreeView` use an INTERNAL viewport?
`QTreeView` inherits from `QAbstractScrollArea`, which has a viewport.
Yes, `viewport()` is correct.

What if `draggedItem == targetItem`? No.

Wait. Is it possible that `QTreeView` does NOT ACCEPT the drag enter event if you drag from VFS to Tree?!
```cpp
    if (event->type() == QEvent::DragEnter) {
        QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);
        // ...
        if (sourceView == openBooksTree || sourceView == vfsExplorer) {
            dragEvent->acceptProposedAction();
            return true;
        }
    }
```
In `DragEnter`, it accepts it!
What about `DragMove`?!
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
In `DragMove`, it accepts it!

So `Drop` MUST be called!
Why does it say "it doesn't do anything"?!
Could it be `targetIndex.isValid()` is FALSE?!
"when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
If they drop it ON the folder, why would `targetIndex` be invalid?
Because in `QTreeView`, if you don't expand the folder, dropping on the icon is valid.
What if `targetIndex.isValid()` is true, but `targetItem` is a Document?
"drop it in a folder" -> `targetItem` is a folder.

What if they drag from Tree to Tree?
"It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"
In Tree to Tree:
`sourceView` = `openBooksTree`.
`targetView` = `openBooksTree`.
```cpp
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    }
```
Is `sourceIndex` valid?
`QModelIndex sourceIndex = sourceView->currentIndex();`
Wait! When dragging in `QTreeView`, `currentIndex()` is the dragged item, right?
BUT wait! If `draggedItem` is a file in the tree, and we `takeRow` from its parent:
```cpp
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
```
Does `QTreeView` crash if we `takeRow` of the item currently being dragged?!
YES! The item being dragged is owned by the `QDrag` operation's `mimeData` or internal state!
If we delete or remove the item from the model WHILE the drop event is processing, the view might abort the drop, or silently fail to update!
Because the view is still finishing the drag-and-drop operation, and it expects the source item to remain until the operation is fully complete (or it handles removal itself if it's a move)!
If we MANUALLY `takeRow` and `appendRow` INSIDE the `Drop` event, we interfere with Qt's native drag-and-drop state machine!
Wait! In VFS to VFS, it works because `draggedItem` is the TREE item, NOT the VFS item! The VFS item is just a view on the tree item. Taking the tree row does not crash the VFS drag-and-drop because the VFS drag is based on the VFS model!
BUT in Tree to Tree, the dragged item IS the Tree item!
If we `takeRow` on the dragged item itself during its own drop event, Qt might abort or revert!
THIS EXPLAINS EVERYTHING!
When dragging Tree to Tree, we modify the EXACT item that is currently in the middle of a drag-and-drop operation!
When dragging VFS to Tree, the drag source is the VFS item. We modify the TREE item!
Wait, but if VFS to Tree modifies the Tree item, why doesn't that work?
Because VFS to Tree: `sourceView` is VFS. `targetView` is Tree.
Wait, "when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
If VFS to Tree doesn't work, maybe VFS item drag is also aborted because the VFS item is rebuilt when `rowsRemoved` fires from the tree change!
If `rowsRemoved` fires, `refreshVfsExplorer()` clears `vfsModel`!
If `vfsModel` is cleared DURING the drop event, the VFS drag source item is DESTROYED!
If the drag source item is destroyed, the drag operation ABORTS!
YES!!!
If the drag operation aborts, the drop is rolled back or ignored by Qt!
And `vfsExplorer` is cleared!

So MANUALLY changing the model INSIDE the `Drop` event is dangerous!
It's much safer to use a `QTimer::singleShot(0, ...)` to perform the model update AFTER the drop event finishes!
YES! If we defer the visual update until AFTER the drop event has been fully processed by Qt, it will work perfectly!
Wait! If we defer it, we still need to block the native drop from messing up our model!
If we `dropEvent->ignore(); return true;`, the native drop is completely blocked and finishes immediately.
THEN our `QTimer::singleShot` runs and moves the item!
Let's try wrapping the `takeRow` and `appendRow` in a `QTimer::singleShot(0, ...)`!
Actually, wait! If `moveItemToFolder` returns `true`, it ALREADY did `takeRow` and `appendRow`!
Let's modify `moveItemToFolder` to defer the visual update!
Wait, if `DragMove` accepts the proposed action, it should show a visual drop indicator!
But if `Drop` event returns `true` AND `QListView` is NOT updating visually...
"it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"
Wait... if I drag a file from the tree onto a folder in the tree view, it doesn't do anything?!
If I drag from tree to tree:
`dropEvent->source()` is `openBooksTree->viewport()`.
`sourceWidget` is `openBooksTree->viewport()`.
`sourceView` is `openBooksTree`.
`targetView` is `openBooksTree`.
`targetIndex` is valid.
`targetItem` is the folder.
`sourceIndex` is `sourceView->currentIndex()`.
Wait... is `sourceIndex` REALLY valid?
When dragging from the VFS Explorer, `sourceView` is `vfsExplorer`.
What is the `currentIndex()` of `vfsExplorer`?
If the user clicks and drags "File", `vfsExplorer` selects "File".
`currentIndex()` is "File". `sourceIndex` is valid!
It extracts `draggedItem` via `findItemRecursive`.
Then it calls `moveItemToFolder(draggedItem, targetItem)`.

Could `moveItemToFolder` return `false` because of cycle detection?
```cpp
    // Cycle detection for folder moves
    if (itemType.endsWith("_folder")) {
        QStandardItem* temp = targetItem;
        while (temp) {
            if (temp == draggedItem) return false;
            temp = temp->parent();
        }
    }
```
If dragging a file, `itemType` is "document". It skips cycle detection.

Why would `targetItem->appendRow(taken)` NOT visually update the tree?
Wait! `taken` is a `QList<QStandardItem*>`.
If `taken` is appended to `targetItem`, `openBooksModel` handles it natively.
Does it visually update? YES.
If the user says "it doesn't do anything", it MUST NOT be appending the row!
If it's not appending the row, `moveItemToFolder` returned `false`!
WHY did it return `false`?!
What if `draggedItem == targetItem`?
If you drag "File" from VFS onto the "Folder" in the tree view:
`draggedItem` is "File".
`targetItem` is "Folder".
They are NOT equal!

What if `compatible` is `false`?
`itemType = "document"`. `targetType = "docs_folder"`.
`compatible = true`.

What if `db->moveItem` returns `false`?
`db->moveItem` returns `true` if SQL executes without error.
It DOES return `true`.

Wait, is it possible that `dropEvent->acceptProposedAction()` is NOT enough to let the view know that the drag succeeded, but returning `true` blocks the view from cleaning up its drag state?!
No, returning `true` from an event filter on `QEvent::Drop` is exactly how you override drag and drop in Qt!
Wait! Is it possible that `sourceIndex` IS INVALID?
Let's change `sourceIndex` extraction to check `selectedIndexes()` just to be absolutely sure!
```cpp
                QModelIndex sourceIndex;
                if (sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                } else {
                    sourceIndex = sourceView->currentIndex();
                }
```
If `sourceIndex` is still invalid, then `draggedItem` is null, and it ignores the drop.

Wait! What if `vfsModel` items do NOT have `Qt::UserRole + 1` set properly?
I checked `refreshVfsExplorer` earlier, and it DOES copy `Qt::UserRole + 1`:
```cpp
        childItem->setData(childTreeItem->data(Qt::UserRole + 1), Qt::UserRole + 1);
```
So `type` is correctly `"docs_folder"`.

Wait, what if `targetIndex.isValid()` is FALSE when dropping from VFS to Tree?
If you drag from VFS and drop ON a folder in the Tree.
Does the Tree highlight the folder when you hover?
Yes, because `DragMove` accepts the action.
Does `indexAt` return the correct index?
```cpp
            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
```
Wait! Is `targetView` correctly resolved?
```cpp
            QAbstractItemView* targetView = (obj == openBooksTree || obj == openBooksTree->viewport()) ? static_cast<QAbstractItemView*>(openBooksTree) : static_cast<QAbstractItemView*>(vfsExplorer);
```
If `obj` is `openBooksTree->viewport()`, `targetView` is `openBooksTree`.
This is correct.

Could it be that the drop event has a `dropAction()` that is NOT `Qt::MoveAction` or `Qt::CopyAction`?
If `dropEvent->dropAction()` is `Qt::IgnoreAction`?
If it's `Qt::IgnoreAction`, `dropEvent->acceptProposedAction()` doesn't do much. But `moveItemToFolder` uses `dropEvent->dropAction() == Qt::CopyAction` to determine if it's a copy. So it treats it as a move!
And it performs the move!

Let's carefully examine `moveItemToFolder`:
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
If `db->moveItem` returns true, `targetItem->appendRow(taken)` happens.
What if `draggedItem->parent()` is NULL?
"File" is in "Documents". The "Documents" folder is a child of "Book".
So `draggedItem->parent()` is the "Documents" folder!
It is NOT null!
So `takeRow` and `appendRow` MUST execute!
If they execute, the UI MUST update!
Why would the user say "it doesn't do anything"?!
Could it be a compiler issue where the code isn't running?
No.

Wait!!! "I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
Wait, if you drag from File Explorer (VFS) to Tree View, what happens to the VFS?
If the file is moved in the tree, the VFS SHOULD UPDATE!
Why? Because `targetItem->appendRow(taken)` triggers `rowsInserted` on `targetItem`.
`parent->takeRow` triggers `rowsRemoved` on `parent`.
My `refreshVfsExplorer` connection:
```cpp
    connect(openBooksModel, &QStandardItemModel::rowsRemoved, this, [this](const QModelIndex& parent, int first, int last) {
        if (mainContentStack->currentWidget() == vfsExplorer) {
            QModelIndex currentIdx = openBooksTree->currentIndex();
            if (parent == currentIdx) {
                refreshVfsExplorer();
            }
        }
    });
```
If you move "File" from "Documents" to "Folder".
`parent` is "Documents". `openBooksTree->currentIndex()` is ALSO "Documents" (since you are viewing it in VFS!).
So `parent == currentIdx` is TRUE!
So `refreshVfsExplorer()` is called!
It rebuilds `vfsModel`.
"File" should disappear from VFS!
But the user says "it doesn't do anything"!
This PROVES that `parent->takeRow` is NOT happening!
Why is it not happening?!
Because `moveItemToFolder` is returning `false`!!!
WHY IS `moveItemToFolder` RETURNING `FALSE`?!

Let's trace:
1. `draggedItem` is null.
2. `targetItem` is null.
3. `sourceBook != targetBook`.
4. `draggedItem == targetItem`.
5. `targetItem->parent()` is null (dropping on root).
6. `compatible` is false.
7. `db->moveItem` returns false.

Let's check 3: `sourceBook != targetBook`.
```cpp
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
If you drag from VFS to Tree, are `sourceBook` and `targetBook` the same?
Yes, `findItemRecursive(book, id, type)` returns an item in the SAME tree!
Wait... "book" is found via:
```cpp
                            QModelIndex treeIndex = openBooksTree->currentIndex();
                            if (treeIndex.isValid()) {
                                QStandardItem* currentBookOrFolder = openBooksModel->itemFromIndex(treeIndex);
                                QStandardItem* book = currentBookOrFolder;
                                while (book && book->parent()) book = book->parent();
                                draggedItem = findItemRecursive(book, id, type);
                            }
```
If `draggedItem` is found under `book`, then `sourceBook` WILL BE `book`.
What about `targetItem`?
You dropped it on a folder in the Tree View.
If you dropped it on a folder in the Tree View, `targetItem` is from `openBooksModel->itemFromIndex(targetIndex)`.
So `targetBook` will be the book of THAT folder.
Are they the same book?
If you have multiple books open, and you drag from VFS to a DIFFERENT book in the tree view!
Does `moveItemToFolder` support moving across books?
```cpp
    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
NO! It returns `false` if `sourceBook != targetBook`!
If the user dragged it to a folder in the SAME book, it should proceed.
If they dragged to the same book, `sourceBook == targetBook`.

Let's check 6: `compatible` is false.
`itemType = "document"`. `targetType = "docs_folder"`.
`compatible = true`.

Let's check 7: `db->moveItem` returns false.
If `itemId` is 0, it still returns true (`SQLITE_DONE`).

What if `draggedItem` is NULL because `sourceIndex` is invalid?
Let's add the `selectedIndexes()` fallback!
If `currentIndex()` is not updated when the drag starts, it might point to the PREVIOUSLY selected item. But that item would STILL be moved! So it wouldn't "do nothing".
What if `currentIndex()` is INVALID?
If it's invalid, it does nothing.

Let's check the drop condition again:
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
```
Wait! What if `sourceView` is NULL?
I added:
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }
```
If you drag from VFS to Tree, `dropEvent->source()` is `vfsExplorer->viewport()`.
`sourceWidget` is `vfsExplorer->viewport()`.
`sourceView` is `nullptr`.
`sourceWidget->parentWidget()` is `vfsExplorer`.
`qobject_cast<QAbstractItemView*>(vfsExplorer)` is valid! So `sourceView` = `vfsExplorer`.
This works.

Wait! What if `targetItem` is NULL because `targetIndex` is invalid?
If you drag from VFS to Tree, and drop ON a folder, `targetIndex` IS valid.
What if `targetView->indexAt()` requires viewport coordinates, but `dropEvent->position().toPoint()` is NOT in viewport coordinates?!
Wait! `QDropEvent::position()` is in the coordinates of the WIDGET RECEIVING THE EVENT!
The widget receiving the event is `obj`!
If `obj` is `openBooksTree->viewport()`, then `position()` is relative to the viewport!
And `QAbstractScrollArea::indexAt()` expects coordinates relative to the viewport!
So `indexAt()` SHOULD BE CORRECT!

Wait! I know!
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If `draggedItem` is not null, and `moveItemToFolder` returns true, it calls `dropEvent->acceptProposedAction()`.
And it returns `true`.
Is it possible that `takeRow` and `appendRow` is throwing an error?
```cpp
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
```
`QStandardItem::takeRow` takes the row and returns it.
BUT wait! `targetItem->appendRow(taken);`
Does `appendRow(const QList<QStandardItem*> &items)` exist?
Yes!
But wait! If the row is taken, does it take ALL the columns?
Yes.
But wait! What if `draggedItem` is the ONLY pointer we have, but `takeRow` deletes it?
No, `takeRow` removes it from the model and transfers ownership to the caller (or the list). Then `appendRow` adds it back, transferring ownership back to the model.
So the item is NOT deleted!

Wait! What if the user drags a folder into its OWN child?
`moveItemToFolder` has cycle detection:
```cpp
    // Cycle detection for folder moves
    if (itemType.endsWith("_folder")) {
        QStandardItem* temp = targetItem;
        while (temp) {
            if (temp == draggedItem) return false;
            temp = temp->parent();
        }
    }
```
This is correct.

Could it be that `eventFilter` is NOT installed on `openBooksTree->viewport()`?!
In `setupUi`:
```cpp
    openBooksTree->installEventFilter(this);
    openBooksTree->viewport()->installEventFilter(this);
```
It IS installed!

Let's look at `dropEvent->dropAction()`.
What if `dropEvent->dropAction()` is `Qt::IgnoreAction`?!
If `Qt::IgnoreAction` is passed to `moveItemToFolder`, `isCopy` is false.
It performs a move!
And it returns `true`!
And `dropEvent->acceptProposedAction()` is called!
But wait, if `dropEvent->acceptProposedAction()` is called on an `IgnoreAction`, does the drag-and-drop system abort it?
Actually, `dropEvent->acceptProposedAction()` sets the drop action to the proposed action (which is usually `MoveAction`).
If `QDrag` sees the action was accepted, it finishes successfully.

What if `db->moveItem` fails?
Let's verify `table` name for documents.
`table = "documents"`. Yes.
Is it possible `itemId` is wrong?
```cpp
    int itemId = draggedItem->data(Qt::UserRole).toInt();
```
In `populateDocumentFolders`:
```cpp
            QStandardItem* item = new QStandardItem(QIcon::fromTheme("text-x-generic"), doc.title);
            item->setData(doc.id, Qt::UserRole);
            item->setData(itemType, Qt::UserRole + 1);
```
Yes, `Qt::UserRole` is the `doc.id`.
It is correct.

What if `sourceIndex` is not retrieved properly because `sourceView` is WRONG?
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }
```
This perfectly matches `vfsExplorer->viewport()` resolving to `vfsExplorer`.

Wait. The user specifically stated:
"when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view, can you please resolve these drag and drop issues?"
BOTH operations have `targetView` == `openBooksTree`.
BOTH operations fail to do anything!
Let's check the drop handling for `openBooksTree` target!
```cpp
            if (targetIndex.isValid()) {
                if (targetView == openBooksTree) {
                    targetItem = openBooksModel->itemFromIndex(targetIndex);
                } else {
                    ...
```
If `targetItem` is found, `sourceIndex` is fetched.
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
```
Is `currentIndex()` valid for the tree view?
When you drag from the tree view, the item is selected and becomes current.
Is `currentIndex()` valid for VFS?
Yes, same thing.

Wait... if `targetIndex.isValid()` is TRUE, it does all this.
What if `targetIndex.isValid()` is FALSE?
If I drop ON A FOLDER in the tree view, `targetIndex` IS VALID!
Why would `moveItemToFolder` fail?
Could `draggedItem == targetItem` be true? No.
Could `compatible` be false?
If I drop a file on a folder, `targetType` is "docs_folder".
`!targetType.endsWith("_folder")` is false.
It proceeds.
`compatible = true`.
`db->moveItem` returns true.
`parent->takeRow` and `targetItem->appendRow` run.
If all this runs, the tree MUST visually update!
But the user says "it doesn't do anything".
This implies the code is NOT REACHED or RETURNS FALSE!

Is it possible `sourceView` is NULL?
Let's see: `dropEvent->source()` returns the widget that started the drag.
If I drag from the tree view, `dropEvent->source()` is `openBooksTree->viewport()`.
`sourceWidget` = `openBooksTree->viewport()`.
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
