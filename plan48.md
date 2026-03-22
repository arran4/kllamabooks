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
`sourceIndex` is valid.
`draggedItem` is the file.
`moveItemToFolder` is called.
`itemType` is "document".
`targetType` is "docs_folder".
`compatible` is `true`.
`db->moveItem` runs!
`parent->takeRow` runs!
`targetItem->appendRow` runs!
It RETURNS `true`!
If it runs, the `draggedItem` MUST physically move in the tree view!
Because `targetItem->appendRow(taken)` directly updates `openBooksModel`!
If it updates `openBooksModel`, `openBooksTree` MUST visually update!
Why would the user say "it doesn't do anything"?!
Could it be `draggedItem` is NULL?!
If `draggedItem` is NULL, it falls to `dropEvent->ignore(); return true;`!
Why would `draggedItem` be NULL?!
```cpp
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
```
If `draggedItem` is NULL, then `openBooksModel->itemFromIndex(sourceIndex)` returned NULL.
But `sourceIndex.isValid()` is TRUE.
When is `itemFromIndex` NULL for a valid index? Never, unless the index is from the WRONG MODEL!
But `openBooksTree` uses `openBooksModel`. So `sourceIndex` MUST be from `openBooksModel`!

Wait... Is it possible `sourceIndex` is NOT valid?!
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (sourceIndex.isValid()) {
```
What if `QTreeView` CLEARS the `currentIndex` when you release the mouse button for a drop?!
If `QTreeView` clears the `currentIndex` BEFORE the `QEvent::Drop` is dispatched to the event filter...
Then `sourceIndex.isValid()` is FALSE!
And it skips the whole block, hitting `dropEvent->ignore(); return true;`!
AND NOTHING HAPPENS!
Ah!!!!!
If `currentIndex()` is cleared during the drop event, we cannot use it!
Does Qt clear `currentIndex()`?
Actually, `QDropEvent` has `mimeData()`. The dragged items are in the MIME data!
Using `currentIndex()` during a drop event is historically flaky because the selection model might update or the drag source might reset state!
But we CAN use the `selectedIndexes()` from the selection model!
Wait, if the selection is also cleared, we're in trouble.
But wait! `QDropEvent` is passed to the event filter BEFORE the view processes it.
Does the view process the mouse release BEFORE the drop event? No, drop event IS the mouse release.

Let's test `sourceView->selectionModel()->selectedIndexes()`.
If that's empty, `sourceIndex` is invalid.

Wait! If `sourceIndex` is invalid, the entire block is skipped.
Let's add an explicit extraction from `selectedIndexes()`.
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (!sourceIndex.isValid() && sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                }
```
Is this enough? If both are empty, what then?
If both are empty, it's impossible to know what was dragged!
Wait! Why did it work BEFORE my `fix_native_drop` patch?
Before my `fix_native_drop` patch, if `draggedItem` was NULL or skipped, it fell through to `return false;` (native drop)!
And the native drop MOVED it!
So `draggedItem` MUST have been NULL!
If `draggedItem` was NULL, `moveItemToFolder` was NEVER CALLED!
If it was NEVER called, the DB was NEVER updated!
And the native drop moved it visually in memory!
Which explains why it "worked" but didn't persist!
YES!!!! THIS PROVES THAT `draggedItem` IS NULL!!!
If `draggedItem` is NULL, it's because `sourceIndex` is INVALID!
Why is `sourceIndex` invalid?!
Because `sourceView->currentIndex()` is INVALID!
Why is `sourceView->currentIndex()` invalid?!
Because `vfsExplorer` and `openBooksTree` might NOT have a valid `currentIndex()` during a drag!
When you drag an item, `QAbstractItemView` creates a `QDrag` and calls `exec()`.
While `exec()` is running, the event loop is inside `QDrag`.
When you release the mouse, `QDrag::exec()` returns.
During the `Drop` event, the view is still in the drag state.
But wait! If you click and drag without releasing the mouse, does the view update `currentIndex()`?
`QTreeView` usually does. `QListView` might not, depending on selection mode.
Wait! What if `sourceView` is WRONG?!
Let's look at `dropEvent->source()`.
If you drag from `openBooksTree`, `dropEvent->source()` is `openBooksTree->viewport()`.
`sourceWidget` = `openBooksTree->viewport()`.
`sourceWidget->parentWidget()` = `openBooksTree`.
`sourceView` = `openBooksTree`.
This IS correct!
So `sourceView` is NOT null.
Then `sourceIndex` must be invalid.

Wait! There is a foolproof way to get the dragged item!
If it's an internal move, we can just let the native drop happen, AND THEN intercept the `rowsMoved` or `rowsInserted` signals to update the DB!
BUT `rowsInserted` doesn't tell us WHICH row moved, just that a new row was inserted!
We'd have to parse the `item->data` and update the DB!
Yes! If we let native drop happen, the `item` is moved to the new parent!
Then `rowsInserted` fires. We can check the item's new parent!
If the new parent is a folder, we can update the DB!
But wait! What if they drop between items? The native drop will put it at a specific row.
But our DB doesn't support order! It will just update `folder_id`.
This is EXACTLY what we want!

BUT wait! If we let native drop happen, what about dragging from VFS to VFS?
VFS is a FLAT LIST! If you natively drop a VFS item onto a VFS folder, `QListView` does NOT support dropping items INTO items! It only supports reordering!
If you drop an item onto a folder in `QListView`, it just reorders it next to the folder! It DOES NOT put it inside the folder in the model!
Because `QListView` model (`vfsModel`) is a flat list! It has no children!
So we CANNOT rely on native drops for VFS!
We MUST use `moveItemToFolder`!
And to use `moveItemToFolder`, we MUST know what item is being dragged!

How can we reliably know what item is being dragged?
When a drag starts, we can intercept `QEvent::DragEnter` or we can just capture the selected items!
Actually, `sourceView->selectionModel()->selectedIndexes().first()` should ALMOST ALWAYS be valid, because you can't drag an item without it being selected in Qt!
Let's check if `sourceIndex` was retrieved incorrectly.
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
```
What if we change it to:
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (!sourceIndex.isValid() && sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                }
                // If STILL invalid, try to get it from selectionModel directly!
                if (!sourceIndex.isValid()) {
                    QModelIndexList sel = sourceView->selectionModel()->selectedIndexes();
                    if (!sel.isEmpty()) sourceIndex = sel.first();
                }
```
Wait! What if we just use `selectedIndexes().first()` ALWAYS?
```cpp
                QModelIndex sourceIndex;
                QModelIndexList selected = sourceView->selectionModel()->selectedIndexes();
                if (!selected.isEmpty()) {
                    sourceIndex = selected.first();
                } else {
                    sourceIndex = sourceView->currentIndex();
                }
```
If `selected.isEmpty()`, `sourceIndex` might still be `currentIndex()`.
Why would it be empty?
If the user drags without selecting? In `QAbstractItemView`, `SingleSelection` or `ExtendedSelection` ALWAYS selects the item when dragging starts.

Let's do this:
If `DragMove` returns `true`, it is accepted.
Then `Drop` event is fired.
Why doesn't it do anything?!
Let's print `qDebug` inside `QEvent::Drop` mentally to see what happens.
`targetIndex` is extracted using `indexAt`.
If I hover over a folder in the tree, `indexAt` returns the folder.
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
`qobject_cast<QWidget*>` works.
`qobject_cast<QAbstractItemView*>(viewport)` returns `nullptr`.
`sourceWidget->parentWidget()` is `openBooksTree`.
`qobject_cast<QAbstractItemView*>(openBooksTree)` returns the pointer!
So `sourceView` is valid!

Wait... I just realized something about `QTreeView`.
If you drop an item onto a folder, the `QDropEvent` has a position.
BUT `indexAt()` only returns the index if the position is EXACTLY over the text or icon!
If the position is over the empty space to the right of the folder name, `indexAt()` might return INVALID!
Yes! `QTreeView` rows span the whole width, but `indexAt` might only hit the actual item!
Wait, no, in `QTreeView`, `indexAt` usually hits the whole row.
BUT what if the drop position is slightly off?
If `targetIndex` is invalid, we do:
```cpp
            if (targetIndex.isValid()) {
                 // ...
            } else if (targetView == vfsExplorer) {
                 // ...
            }
            // ...
            if (targetItem) {
                // ...
            }
            dropEvent->ignore();
            return true;
```
If `targetIndex` is invalid in the tree view, it ignores the drop!
BUT the user said "drop it in a folder in the tree view". If they aim at the folder, `targetIndex` should be valid.

Let's rethink `draggedItem = openBooksModel->itemFromIndex(sourceIndex);`.
Is it possible `sourceIndex` is from the WRONG MODEL?
`sourceView->currentIndex()` returns a `QModelIndex` from `sourceView->model()`.
For `openBooksTree`, the model is `openBooksModel`. So `itemFromIndex` works.
For `vfsExplorer`, the model is `vfsModel`.
Wait! For VFS explorer, we do:
```cpp
                    } else if (sourceView == vfsExplorer) {
                        QStandardItem* vfsItem = vfsModel->itemFromIndex(sourceIndex);
                        if (vfsItem && vfsItem->text() != "..") {
                            int id = vfsItem->data(Qt::UserRole).toInt();
                            QString type = vfsItem->data(Qt::UserRole + 1).toString();
                            QModelIndex treeIndex = openBooksTree->currentIndex();
                            if (treeIndex.isValid()) {
                                QStandardItem* book = openBooksModel->itemFromIndex(treeIndex);
                                while (book && book->parent()) book = book->parent();
                                draggedItem = findItemRecursive(book, id, type);
                            }
                        }
                    }
```
This is perfectly correct.

What if `moveItemToFolder` is returning `false` because `sourceBook != targetBook`?
```cpp
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
When does a `QStandardItem` NOT have a parent?
When it is the ROOT item!
In `openBooksModel`, the book items are ROOT items!
So `sourceBook->parent()` is `nullptr`. The `while` loop terminates.
`sourceBook` is the book item!
`targetBook` is the book item!
They are compared: `sourceBook != targetBook`.
Since they are pointers to the exact same `QStandardItem` in `openBooksModel`, they MUST be equal!
So it doesn't return `false` here!

Wait... what if `draggedItem` or `targetItem` is NULL?!
If `moveItemToFolder` receives a NULL pointer, it returns `false`.
But we check `if (draggedItem && ...)` before calling it.

Is there ANY WAY `moveItemToFolder` returns `false`?
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
If you drop onto a document, its parent is `docs_folder`.
`targetType` becomes "docs_folder". `endsWith("_folder")` is TRUE.
It doesn't return `false`.

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
        // ...
```
What if `itemType` is NOT "document"?
In `populateDocumentFolders`, `itemType` is set to "document".
What if it's a folder?
`itemType` is "docs_folder".
`targetType` is "docs_folder".
`itemType.endsWith("_folder") && targetType == itemType` is TRUE!
It calls `db->moveFolder`.
It returns `true`!

So `moveItemToFolder` MUST BE RETURNING `TRUE`!
If it returns `true`, why doesn't it update visually?!
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
If `targetItem->appendRow(taken)` executes, the UI MUST UPDATE!
If the UI DOES NOT UPDATE, then `appendRow` is NOT EXECUTING!
If `appendRow` is NOT EXECUTING, `db->moveItem` returned FALSE!
OR `parent` is NULL!
Is `draggedItem->parent()` NULL?
For a file, its parent is the folder it's in.
It is NOT null!

So `db->moveItem` MUST BE RETURNING `FALSE`!
Why would `db->moveItem` return `false`?
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
`sqlite3_prepare_v2` could fail if the table doesn't exist? But we know it exists.
`sqlite3_bind_int` succeeds.
`sqlite3_step` returns `SQLITE_DONE` on success.
If it succeeds, it returns `true`.

Wait! Is there ANY CHANCE `m_isOpen` is `false`?
No, we are actively interacting with the DB.

What if I'm completely wrong and `eventFilter` is NOT EVEN CATCHING the drop event?!
Why wouldn't it catch the drop event?
Because `installEventFilter` is NOT called on the viewport?
```cpp
    openBooksTree->viewport()->installEventFilter(this);
```
I added it!
Wait! Did I add it AFTER `openBooksTree` was created?
Yes, in `setupUi`:
```cpp
    openBooksTree = new QTreeView(this);
    // ...
    openBooksTree->installEventFilter(this);
    openBooksTree->viewport()->installEventFilter(this);
```
This is correct.

What if `QAbstractItemView` consumes the drop event before `eventFilter` gets it?
No, `eventFilter` is called BEFORE the object's own event handler.

What if `sourceIndex.isValid()` is `false` because `sourceView->currentIndex()` is NOT the dragged item?
If `currentIndex()` is not updated, we should use `selectedIndexes()`.
Let's change to `selectedIndexes()` to be absolutely sure!
If `DragMove` returns `true`, it is accepted.
Then `Drop` event is fired.
Why doesn't it do anything?!
Let's print `qDebug` inside `QEvent::Drop` mentally to see what happens.
`targetIndex` is extracted using `indexAt`.
If I hover over a folder in the tree, `indexAt` returns the folder.
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
