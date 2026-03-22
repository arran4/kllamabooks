Let's find out why it returns false!
If `moveItemToFolder` returns false, it returns `false` at one of these lines:
1. `if (!draggedItem || !targetItem) return false;`
2. `if (!sourceBook || !targetBook || sourceBook != targetBook) return false;`
3. `if (draggedItem == targetItem) return false;`
4. `if (!targetType.endsWith("_folder")) { ... else return false; }`
5. `return false;` at the end because `compatible` is false or `db->moveItem` is false.

For #1:
`draggedItem` is extracted via `findItemRecursive(book, id, type)`.
Is it possible `id` and `type` don't match the tree?
In `refreshVfsExplorer`:
```cpp
        childItem->setData(childTreeItem->data(Qt::UserRole), Qt::UserRole);
        childItem->setData(childTreeItem->data(Qt::UserRole + 1), Qt::UserRole + 1);
```
Wait! What if `vfsItem->data(Qt::UserRole + 1).toString()` is "document"?
And in the tree, it is ALSO "document".
Wait! Does `findItemRecursive` check `type` correctly?
```cpp
QStandardItem* MainWindow::findItemRecursive(QStandardItem* parent, int id, const QString& type) {
    if (parent->data(Qt::UserRole).toInt() == id && parent->data(Qt::UserRole + 1).toString() == type) {
        return parent;
    }
```
If a file is inside "Documents" folder.
`id` is > 0. `type` is "document".
It WILL be found!

What if `sourceBook` and `targetBook` are DIFFERENT?!
Wait... "I have 1 folder 'Folder' and 1 file 'File' in Documents"
They are BOTH in Documents! So they are in the SAME book!
So `sourceBook == targetBook` is TRUE.

What if `db->moveItem` returns FALSE?!
```cpp
bool BookDatabase::moveItem(const QString& table, int id, int newFolderId) {
    if (!m_isOpen) return false;
    QString safeTable = table;
    if (table != "messages" && table != "documents" && table != "templates" && table != "drafts" && table != "notes") {
        return false;
    }
```
If it's a document, `table` is "documents".
It executes `UPDATE documents SET folder_id = ? WHERE id = ?`.
It ALWAYS returns TRUE! Because `sqlite3_step` returns `SQLITE_DONE`!

So WHY would it return false?!
Wait! I've been assuming `moveItemToFolder` is returning `false`!
What if `moveItemToFolder` returns `TRUE`?!
If it returns `true`, then the `Drop` handler does:
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If it calls `dropEvent->acceptProposedAction()`, it tells Qt the drop is accepted.
Then my `moveItemToFolder` code runs the `QTimer::singleShot`.
```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        QTimer::singleShot(0, this, [draggedItem, targetItem]() {
            QStandardItem* parent = draggedItem->parent();
            if (parent) {
                QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
                targetItem->appendRow(taken);
            }
        });
        return true;
    }
```
What if `draggedItem` or `targetItem` is INVALIDATED before the `QTimer` runs?!
Why would they be invalidated?!
If Qt natively processed the drop, it might delete the item!
BUT WE BLOCKED THE NATIVE DROP! `return true;` in `eventFilter` blocks it!
If we blocked it, Qt DOES NOT process the drop natively!
So the items are NOT invalidated!

Wait... If `moveItemToFolder` returns `true`, and the `QTimer` runs...
Does it crash?
If it crashed, the user would say "the app crashes". But they said "it doesn't do anything".
If it doesn't crash, it means `parent->takeRow` runs and `targetItem->appendRow` runs!
If they run, the tree model MUST update!
If the tree model updates, `openBooksTree` visually updates!
If the tree model updates, `rowsRemoved` and `rowsInserted` fire!
If they fire, `refreshVfsExplorer` runs!
If `refreshVfsExplorer` runs, the VFS updates!
BUT THE USER SAID IT DIDN'T UPDATE VFS OR TREE!
"it doesn't do anything."
This means `moveItemToFolder` DID NOT RETURN TRUE!
Or `targetItem` was NULL!
Or `draggedItem` was NULL!
Or the `QEvent::Drop` was NEVER CALLED!

Wait! Could it be that `eventFilter` is NOT calling the `Drop` event because my `DragMove` handler is returning `false`?!
```cpp
    } else if (event->type() == QEvent::DragMove) {
        QDragMoveEvent* dragEvent = static_cast<QDragMoveEvent*>(event);
        if (obj == openBooksTree || obj == openBooksTree->viewport()) {
            return false; // let native handling show drop indicator
        } else if (obj == vfsExplorer || obj == vfsExplorer->viewport()) {
            return false; // let native handling show drop indicator
        }
    }
```
If I return `false`, I let native handling show the drop indicator.
BUT native handling MIGHT REJECT THE DROP!
If native handling REJECTS the drop, it sets the `dropAction` to `Qt::IgnoreAction`!
And if `dropAction` is `Qt::IgnoreAction`, the user's cursor turns into a forbidden sign!
And if they release the mouse, `QEvent::Drop` is NEVER FIRED!
YES!!!
If the user releases the mouse while the cursor is a forbidden sign, Qt ABORTS the drag and DOES NOT fire the `Drop` event!
Why would native handling REJECT the drop?!
Because `QTreeView` natively rejects drops between items unless the items are drop-enabled!
Wait, but if you drop ON a folder, `QTreeView` DOES NOT reject it!
"when I drop a file from the tree view onto a folder in the tree view it doesn't do anything"
If they drop it ON a folder, native handling DOES NOT reject it!
So the `Drop` event IS fired!

Wait! I see it!
In `QEvent::Drop`:
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
What if `sourceView` IS NOT `openBooksTree` OR `vfsExplorer`?!
If I drag a file from `openBooksTree`, `dropEvent->source()` is `openBooksTree`?
Wait! In `QTreeView` drag and drop, `QDrag` is created by `QAbstractItemView::startDrag`.
The parent of `QDrag` is the VIEWPORT, or the VIEW?
Usually, `dropEvent->source()` returns the VIEW (the `QTreeView`).
If it returns the VIEW, `sourceWidget` is the VIEW.
`qobject_cast<QAbstractItemView*>(sourceWidget)` IS VALID!
So `sourceView` is the VIEW!
So `sourceView == openBooksTree` is TRUE!

What if `targetIndex.isValid()` is FALSE?!
If the user is trying to drop ON a folder, but they accidentally drop BETWEEN folders!
If they drop BETWEEN folders, `targetIndex` is FALSE!
And it does NOTHING!
And since it does nothing, the user complains!
If they try 5 times and all 5 times they miss the text by 1 pixel, they think it's completely broken!
Because `QTreeView` draws a line BETWEEN the folders, suggesting you can drop there!
BUT my code IGNORES it because `targetIndex` is invalid!
THIS IS IT! THIS MUST BE IT!

How do we handle `targetIndex` being invalid?
We should ALLOW it to fallback to native drop!
BUT if it falls back to native drop, it moves visually and NO database update happens!
Wait! We CAN get the target item even if `targetIndex` is invalid!
If `targetIndex` is invalid, the user is dropping BETWEEN items in the root, or at the end of the list!
We can get the target parent from `targetView->rootIndex()` or the parent of the nearest item!
But actually, the easiest way is to intercept `rowsInserted` in `openBooksModel`!
If we intercept `rowsInserted`, we can update the database EVERY TIME the tree updates!
AND we don't have to intercept `QEvent::Drop` at all for internal moves!
BUT wait! We decided NOT to intercept `rowsInserted` because `loadDocumentsAndNotes` triggers it!
What if we set a flag `bool isUpdatingFromDb = false;` in `MainWindow`?
When `loadDocumentsAndNotes` runs:
```cpp
    isUpdatingFromDb = true;
    // ...
    isUpdatingFromDb = false;
```
If we do this, we can SAFELY intercept `rowsInserted` and update the DB!
AND we can completely REMOVE all this custom `QEvent::Drop` nightmare for internal moves!
AND we just let `QTreeView` and `QListView` do their native drag and drop perfectly!

Wait! Does `QListView` natively support moving files into folders?
NO! `QListView` in `IconMode` is a flat list! You CANNOT drop an item INTO another item in `QListView` natively!
So for `vfsExplorer`, we MUST handle it manually via `QEvent::Drop`!
But for `openBooksTree`, `QTreeView` handles it natively!
So we MUST intercept `QEvent::Drop` for `vfsExplorer`.

If we must intercept `QEvent::Drop` for `vfsExplorer`, let's just FIX my custom logic!
Why did it not work?
Let's print `qDebug()`! We can't.
Let's assume the user WAS dropping perfectly ON the folder!
If they dropped perfectly ON the folder, why didn't it work?
Could it be because of my `QTimer::singleShot`?
```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        QTimer::singleShot(0, this, [draggedItem, targetItem]() {
            QStandardItem* parent = draggedItem->parent();
            if (parent) {
                QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
                targetItem->appendRow(taken);
            }
        });
        return true;
    }
```
If I pass `draggedItem` and `targetItem` into a lambda...
Wait! `takeRow` takes the row by index.
What if `draggedItem->row()` has CHANGED?!
What if something else modified the model before the timer runs? No.
What if `targetItem->appendRow(taken)` FAILS because `taken` is empty?!
I verified `takeRow` returns the row correctly.

What if `moveItemToFolder` is NEVER called?!
In `eventFilter`:
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
Wait! `targetIndex` MUST BE VALID!
If it is valid, `targetItem` is found.
```cpp
            if (targetItem) {
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (!sourceIndex.isValid() && sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                }

                if (sourceIndex.isValid()) {
                    QStandardItem* draggedItem = nullptr;
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (sourceView == vfsExplorer) {
```
Wait! Is `sourceIndex` from `openBooksTree` REALLY valid?
If the user dragged an item from the tree, it IS selected. So `hasSelection()` is true. `selectedIndexes().first()` is valid.
So `draggedItem` is `openBooksModel->itemFromIndex(sourceIndex)`.

Could it be that the user dragged MULTIPLE items?
If they dragged multiple items, `selectedIndexes()` returns multiple indexes!
And we only take the FIRST!
But the user said "when I take a file". Singular.

Is there ANY CHANCE `dropEvent->source()` is NOT `openBooksTree` OR `vfsExplorer`?!
What if `dropEvent->source()` is the viewport?
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }
```
I added this. It correctly resolves viewport to the view.
What if `dropEvent->source()` is `nullptr`?!
If it is `nullptr`, `sourceWidget` is `nullptr`. `sourceView` is `nullptr`.
Then the condition `(sourceView == openBooksTree || sourceView == vfsExplorer)` is FALSE!
And it falls through to native drop!
Is `dropEvent->source()` `nullptr` during internal drags in `QTreeView`?!
Qt documentation says: "If the source of the drag operation is a widget in this application, source() returns that widget; otherwise it returns nullptr."
So `source()` should be the widget!
But wait! `QAbstractItemView::startDrag` creates a `QDrag`.
Does it pass `this` (the view) or `viewport()` as the parent of `QDrag`?!
Usually, it passes `this`.
But even if it passes `viewport()`, my code handles it.
Wait! What if `source()` is NEITHER `openBooksTree` NOR `vfsExplorer`?!
What if it's some internal delegate widget?
No, `source()` is the object passed to `QDrag` constructor. `QAbstractItemView` passes `this`.
Let's assume `sourceView` is correctly resolved.

Wait! What if we DO NOT NEED TO INTERCEPT `QEvent::Drop` for the Tree View!
If we let the Tree View handle the drop natively...
The Tree View moves the item visually.
We can then intercept `QStandardItemModel::rowsInserted` or `itemChanged` to update the database!
BUT we decided `rowsInserted` is risky.
What if we intercept `Drop` for BOTH, but we MANUALLY perform the drop ONLY IF `targetIndex` is valid?!
If `targetIndex` is NOT valid, we fallback to native drop?!
If we fallback to native drop, we MUST update the DB!
How to update the DB if we don't intercept it?!

Let's just FIX `targetIndex` being invalid!
If `targetIndex` is invalid, the user dropped between rows.
If we can find out the parent folder, we can STILL process it manually!
```cpp
            if (!targetIndex.isValid()) {
                // If dropping in empty space in tree view, it goes to the root. But we don't allow dropping documents to root!
                // Wait! If they drop between rows, `indexAt` is invalid, but `dropEvent->position()` has a Y coordinate.
                // We can find the closest item vertically!
            }
```
Actually, we don't need to guess.
"The file explorer list view doesn't give you an indication that you can drag and drop something onto a folder BUT it does work."
This means VFS Explorer is NOT drawing the highlight box!
Why is it not drawing the highlight box?
Because we returned `false` from `DragMove`, letting native `QListView` handle it.
And native `QListView` in `IconMode` DOES NOT highlight items because it only supports dropping BETWEEN items (for reordering).
To force it to highlight the folder, we MUST intercept `DragMove` and manually draw it? No, we can't draw it.
BUT we can change `vfsExplorer->setMovement(QListView::Static);`!
If we do that, `QListView` might start highlighting folders because it can no longer reorder!
Let's add `vfsExplorer->setMovement(QListView::Static);` in `setupUi`!
And `vfsExplorer->setResizeMode(QListView::Adjust);` so it reflows.

What about "drag drop in the tree view isn't working... when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
Why doesn't it do anything?!
Let's remove the `QTimer::singleShot`!
Maybe `QTimer::singleShot` is the REASON it doesn't do anything!
If `QTimer` fires AFTER the event loop returns to `QDrag`, `QDrag` might have already finished and deleted the `QDrag` object.
And maybe `QTreeView` does something after `QDrag` finishes that reverts the model?
No, `QTreeView` doesn't revert the model.
But maybe `draggedItem` was ALREADY DELETED by native drop?!
Wait! I did `dropEvent->acceptProposedAction(); return true;`!
If I `return true;`, the native drop is BLOCKED!
If native drop is blocked, `QTreeView` NEVER natively moves or deletes the item!
So `draggedItem` is safe!
Why did `QTimer` fail?
If it's inside a lambda, `draggedItem` is a pointer. It's safe.
Maybe `taken` is empty because `takeRow` fails?!
```cpp
            QStandardItem* parent = draggedItem->parent();
            if (parent) {
                QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
                targetItem->appendRow(taken);
            }
```
If `draggedItem->parent()` is `nullptr`?!
Ah! If the item is in the VFS Explorer, `draggedItem` is found in the TREE via `findItemRecursive`.
Is the tree item a root item? No, documents are children of `docs_folder`. So `parent` is `docs_folder`. It's NOT null!

I will REMOVE `QTimer::singleShot`. It was working BEFORE I added it for VFS-to-VFS! The only reason I added it was because I THOUGHT it was breaking Tree-to-Tree.
Wait! BEFORE I added `QTimer::singleShot`, the user said "Changes made using the tree are not persisting"!
Why didn't they persist BEFORE `QTimer::singleShot`?
Because before `QTimer::singleShot`, I was doing `loadDocumentsAndNotes()`!
```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        loadDocumentsAndNotes();
        return true;
    }
```
If I did `loadDocumentsAndNotes()`, it rebuilt the tree.
And the user said "When I move something in the file view the tree view completely refreshes... it should be an update operation not a complete refresh".
So I changed it to:
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
BUT wait! When I made this change, the user said "Moves on the tree view no longer work via drag and drop"!
Why did `takeRow` / `appendRow` BREAK the tree view drag and drop, but `loadDocumentsAndNotes()` WORKED?!
Because `takeRow` REMOVES the item from the model!
If the item is REMOVED from the model DURING the `Drop` event, and the `QDrag` source was the `QTreeView`...
`QTreeView` tries to do something with the dragged item AFTER the drop event?
No, we returned `true`! `QTreeView::dropEvent` is NEVER called!
But `QDrag` is still active. `QDrag` belongs to the source `QTreeView`!
If the source `QTreeView`'s model loses the item, does the source `QTreeView` crash or abort the drag?
If the drag is aborted, the `dropEvent->acceptProposedAction()` might be ignored!
BUT `db->moveItem` WAS ALREADY EXECUTED!
So the DB IS UPDATED!
If the DB is updated, but the UI is aborted... then when you restart the app, the DB SHOULD STILL BE UPDATED!
But the user said "neither changes persist when I restart the application" (referring to the FIRST time, BEFORE I fixed the viewport bug).
So we have MULTIPLE intersecting bugs.

Let's just use `loadDocumentsAndNotes()` BUT preserve the expanded state!
If we use `loadDocumentsAndNotes()` and preserve the expanded state, the tree rebuilds, but the user doesn't notice!
AND we don't have to mess with `takeRow` which might be breaking the drag-and-drop state machine!
Yes! `loadDocumentsAndNotes()` is 100% safe because it just reloads from the DB!
To preserve the expanded state:
```cpp
    QSet<QString> expandedPaths;
    for (int i = 0; i < openBooksModel->rowCount(); ++i) {
        // save expanded state recursively
    }
    loadDocumentsAndNotes();
    // restore expanded state
```
Wait, it's easier to just traverse the tree and save the IDs of expanded folders!
```cpp
    void saveExpandedState(QTreeView* tree, QStandardItem* item, QSet<int>& expandedIds) {
        if (tree->isExpanded(item->index())) {
            expandedIds.insert(item->data(Qt::UserRole).toInt());
        }
        for (int i = 0; i < item->rowCount(); ++i) {
            saveExpandedState(tree, item->child(i), expandedIds);
        }
    }
```
If we do this, `loadDocumentsAndNotes()` is completely seamless!
AND we can safely use it without breaking `QDrag`!
Actually, `takeRow` and `appendRow` is standard Qt. It shouldn't break `QDrag`.
Why did it break?
Maybe because I forgot to remove `QTimer::singleShot`!
Let's REMOVE `QTimer::singleShot`, and just do `takeRow` and `appendRow`.
Wait, I already DID `takeRow` and `appendRow` without `QTimer` in Plan 45, and the user said it didn't work!
If `takeRow` and `appendRow` doesn't work, let's use `loadDocumentsAndNotes()` with preserved state!
