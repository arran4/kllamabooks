So `DragMove` and `DragEnter` logic:
I already changed them to `return false; // let native handling show drop indicator`.
So native `QTreeView` handles it visually!
But wait! If native `QTreeView` handles it visually, it shows the indicator!
When dropped, my `Drop` handler intercepts it, does `moveItemToFolder`, returns `true`.
The `QTreeView` DOES NOT see the drop natively.
`targetItem->appendRow(taken)` executes!
Does the UI update visually immediately?!
YES! `QStandardItemModel` updates immediately!
Why does the user say it doesn't do anything?!
Wait... "When I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
If they drop it "in" a folder, they must be hovering over the folder text!
If they hover over the folder text, the drop indicator HIGHLIGHTS the folder text.
Then they release the mouse. `QEvent::Drop` fires.
`targetIndex` is valid.
`targetItem` is the folder.
`sourceIndex` is valid.
`draggedItem` is the file.
`moveItemToFolder` is called.
It executes `appendRow(taken)`.
The file is moved in the tree visually.
Wait! If it is moved visually, why doesn't it persist?!
Because it "doesn't do anything"!
If it doesn't do anything, it means `moveItemToFolder` returns `false` or is skipped!

Let's check the arguments passed to `moveItemToFolder` from VFS to Tree:
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
Is `treeIndex.isValid()` true?
Yes, `openBooksTree->currentIndex()` is the folder currently viewed in VFS.
What if the user clicked the VFS item, and `openBooksTree` lost its `currentIndex()`?!
`QTreeView` doesn't clear `currentIndex()` when it loses focus!
Wait! "I drag and drop it using the list view file explorer ... it doesn't persist, or update the tree."
If they click and drag in `vfsExplorer`, does `openBooksTree` STILL have its `currentIndex()`?
Yes!

What if `id` or `type` is wrong in VFS item?
We know they are copied in `refreshVfsExplorer`.

Is there ANY other reason `moveItemToFolder` returns false?
```cpp
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
If dragging from VFS to Tree, `draggedItem` is found using `book` from `openBooksTree`.
So `sourceBook` MUST be `book`.
What about `targetItem`?
`targetItem` is found from `openBooksModel->itemFromIndex(targetIndex)`.
Does `targetBook` equal `book`?
Yes, if they drop it on a folder in the SAME book.
If they drop it on a folder in a DIFFERENT book, it returns `false`, which correctly does nothing.
But the user says "in Documents". So it's in the same book.

What if `targetFolderId` is not found?
If they drop it on a file, `targetFolderId` is resolved to the parent folder.
Wait! If they drop it on a folder, `targetType` is "docs_folder", so `targetFolderId` is the folder's ID.
It calls `db->moveItem("documents", itemId, targetFolderId)`.

Wait, could `moveItemToFolder` crash if `draggedItem == targetItem`?
No, there is `if (draggedItem == targetItem) return false;`.

Let's assume the previous `fix_drag_allow_files` and `fix_viewport` actually SOLVED the underlying cause, but the user tested it with an older build?
No, the user tested the LATEST build.
"when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
Wait... if I drag from VFS to Tree, `sourceView` is `vfsExplorer`. `targetView` is `openBooksTree`.
In `QEvent::Drop`:
```cpp
        if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
            QAbstractItemView* targetView = (obj == openBooksTree || obj == openBooksTree->viewport()) ? static_cast<QAbstractItemView*>(openBooksTree) : static_cast<QAbstractItemView*>(vfsExplorer);
```
`targetView` is `openBooksTree`.
```cpp
            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
            QStandardItem* targetItem = nullptr;
```
`targetIndex` is valid if dropped on a folder.
```cpp
            if (targetIndex.isValid()) {
                if (targetView == openBooksTree) {
                    targetItem = openBooksModel->itemFromIndex(targetIndex);
```
`targetItem` is found.
```cpp
            if (targetItem) {
                QModelIndex sourceIndex = sourceView->currentIndex();
                // ...
                if (sourceIndex.isValid()) {
                    QStandardItem* draggedItem = nullptr;
                    // ...
```
`draggedItem` is found.
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If `moveItemToFolder` returns `true`, it accepts the action!
And `targetItem->appendRow(taken)` executes!
If this executes, the tree MUST visually update!
Why would the user say "it doesn't do anything"?!
Could it be `draggedItem` is null?!
Why would `draggedItem` be null?
In `sourceView == vfsExplorer`:
```cpp
                            QModelIndex treeIndex = openBooksTree->currentIndex();
                            if (treeIndex.isValid()) {
                                QStandardItem* book = openBooksModel->itemFromIndex(treeIndex);
                                while (book && book->parent()) book = book->parent();
                                draggedItem = findItemRecursive(book, id, type);
                            }
```
If `treeIndex` is INVALID, `draggedItem` is null!
When is `treeIndex` invalid?!
Wait... when you drag from VFS Explorer to the Tree View, does the Tree View change its `currentIndex()`?!
If you hover over folders in the Tree View, the Tree View does NOT change its selection natively! It only changes its drop target indicator!
But if you DROP it on a folder, does `QTreeView` select that folder BEFORE `QEvent::Drop` is fired?!
If it selects the target folder BEFORE `Drop` is fired, then `treeIndex` points to the TARGET folder!
Wait! If `treeIndex` points to the TARGET folder...
```cpp
                                QStandardItem* book = openBooksModel->itemFromIndex(treeIndex);
                                while (book && book->parent()) book = book->parent();
```
`book` is STILL the root book!
Because the target folder is IN THE SAME BOOK!
And `findItemRecursive` searches the ENTIRE book for the `id` and `type`!
So it WILL find `draggedItem` ANYWAY!
Because `findItemRecursive` starts from the `book` root!
So it doesn't matter if `treeIndex` changed to the target folder!
Ah!!!
So `draggedItem` CANNOT be null!

Wait! What if `id` or `type` is wrong?
`type` is `"docs_folder"` or `"document"`.
Let's verify `vfsItem->data(Qt::UserRole).toInt()` is correct.
Yes, we copied it in `refreshVfsExplorer`.

What if `moveItemToFolder` fails because of `targetType`?!
```cpp
    QString targetType = targetItem->data(Qt::UserRole + 1).toString();
    int targetFolderId = targetItem->data(Qt::UserRole).toInt();

    // Handle dropping on an item (non-folder)
    if (!targetType.endsWith("_folder")) {
```
If `targetItem` is the "Folder" in the tree, its type is `"docs_folder"`.
`endsWith("_folder")` is true! It skips the `if`.
`compatible = true`.
`db->moveItem` returns true.
`moveItemToFolder` returns true.

THIS MUST WORK!
Why did the user say "it doesn't do anything"?
Maybe the user's report "it doesn't do anything" meant the visual `targetItem->appendRow(taken)` FAILS?!
Why would `appendRow(taken)` fail?!
Wait! `taken` is a `QList<QStandardItem*>`.
If `taken` is empty, it does nothing!
Why would `taken` be empty?
```cpp
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
```
Does `parent->takeRow(draggedItem->row())` return an empty list?
If `draggedItem->row()` is valid, it returns the items!
Wait! What if `draggedItem` was ALREADY REMOVED from the model?
Who would remove it?!
If the VFS Explorer natively processed the drag move... no, we block native processing!

Wait! I see the issue!
"It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"
If you drag from Tree View to Tree View!
`sourceView` = `openBooksTree`.
```cpp
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    }
```
`draggedItem` is NOT null.
`moveItemToFolder` is called.
It does `takeRow` and `appendRow`.
Why doesn't it visually update?!
Wait! `targetItem->appendRow(taken)`!
If you drag a file onto a folder, `targetItem` is the folder!
`appendRow` adds the file to the folder!
Does the Tree View automatically expand the folder?
No. But if the folder was already expanded, you see the file!
If the folder wasn't expanded, it gets an expand arrow!
And you can expand it!
If it "doesn't do anything", it means the file stays exactly where it was!
Which means `takeRow` and `appendRow` did NOTHING!
Or `moveItemToFolder` returned FALSE!
WHY WOULD IT RETURN FALSE?!
Let's check `compatible`:
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
```
Wait! What if `itemType` is "document", but `targetType` is "document"?!
If you drop a file onto a file, `targetItem`'s original type is "document".
Then:
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
If you drop a document onto a document, `targetItem` becomes the parent folder!
`targetType` becomes "docs_folder"!
Then `compatible = true`!
It should work!

What if `targetType` is empty?
When loading books, `docsItem->setData("docs_folder", Qt::UserRole + 1);`
It's set.

What if `db->moveItem` is FAILING?!
Could `table` be incorrect? "documents". Yes.
Could `itemId` be 0?
Wait... if the user created "File" via "New Document", it starts as a draft!
If they clicked "Save", it becomes a "document" with a valid ID.
If they just created it, it's a "draft"!
If they drag a "draft" to a "docs_folder", what happens?
```cpp
    else if (itemType == "draft" && targetType == "drafts_folder")
```
It requires `targetType == "drafts_folder"`!
If they drag a "draft" into "docs_folder" (Documents), `compatible` is FALSE!
And it returns `false`!
YES!!!
If the user created a "New Document" and it was auto-saved as a draft, it's a DRAFT!
But wait! The user said: "I have 1 folder 'Folder' and 1 file 'File' in Documents."
If it's in Documents, it MUST be a "document"! Because `populateDocumentFolders` loads "documents" into "Documents"!
And `itemType` is "document"!
So it MUST be compatible!

Let's double-check the `Drop` event logic.
Is there any chance `sourceIndex` is INVALID?
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (!sourceIndex.isValid() && sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                }
```
If the user clicks and drags, `QTreeView` updates the selection on press.
So `sourceIndex` SHOULD be valid.

Let's assume the previous `DragMove` logic was NOT ACTUALLY APPLIED properly?
```cpp
    } else if (event->type() == QEvent::DragMove) {
        QDragMoveEvent* dragEvent = static_cast<QDragMoveEvent*>(event);
        if (obj == openBooksTree || obj == openBooksTree->viewport()) {
            return false; // let native handling show drop indicator
        } else if (obj == vfsExplorer || obj == vfsExplorer->viewport()) {
            return false; // let native handling show drop indicator
        }
```
I changed it to `return false;`!
If `eventFilter` returns `false`, the event IS PASSED TO `QTreeView`!
`QTreeView` handles `DragMove` natively!
If `QTreeView` handles it natively, it updates the drop indicator!
And it ACCEPTS OR IGNORES the drop depending on its internal model!
Wait... `QStandardItemModel::dropMimeData` handles drops.
But does `QStandardItemModel` ACCEPT drops on ANY item?
Yes, `QStandardItem` has `ItemIsDropEnabled` by default!
So it ACCEPTs the drop natively!
If it accepts the drop natively, it emits `QEvent::Drop`!
And my `QEvent::Drop` event filter CATCHES IT!
```cpp
    } else if (event->type() == QEvent::Drop) {
```
It catches it, runs my logic, and `return true;` consumes the event!
So native drop is NOT performed!
And my manual `takeRow`/`appendRow` IS performed!

Wait... If `QTreeView` accepts the drop natively, it determines the drop action (e.g. `Qt::MoveAction`).
If my manual `takeRow`/`appendRow` is performed, the tree visually updates!
Why doesn't it do anything?!
Maybe I need to `QTimer::singleShot(0, ...)` the `takeRow`/`appendRow`?
Because modifying the model DURING the drop event handler might cause the view to abort or fail to render the change!
Yes! `QTreeView` is in the middle of a drag-and-drop state machine!
If I delete the dragged row (`takeRow`) while it's handling the drop event, the view might crash or silently fail to update its internal state!
To be absolutely safe, we should `QTimer::singleShot(0, ...)` the actual model manipulation!
So `DragMove` and `DragEnter` return `false`! They let the native event run!
If native event runs, it drops exactly where it wants!
But wait! "when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
If I drag from File Explorer (VFS) to Tree View, what happens?
`obj` = `openBooksTree->viewport()`.
`sourceWidget` = `vfsExplorer->viewport()`.
`sourceView` = `vfsExplorer`.
Wait! Does `dropEvent->source()` return `vfsExplorer->viewport()` when I drag from VFS?!
YES!
Does my code correctly resolve `sourceView`?
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }
```
Yes, this resolves `sourceView` to `vfsExplorer`.
Then it enters:
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
```
This is TRUE.
```cpp
            QAbstractItemView* targetView = (obj == openBooksTree || obj == openBooksTree->viewport()) ? static_cast<QAbstractItemView*>(openBooksTree) : static_cast<QAbstractItemView*>(vfsExplorer);
```
`targetView` is `openBooksTree`.
```cpp
            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
```
`targetIndex` is the index under the cursor in the tree.
```cpp
            if (targetIndex.isValid()) {
                if (targetView == openBooksTree) {
                    targetItem = openBooksModel->itemFromIndex(targetIndex);
```
`targetItem` is the folder in the tree.
```cpp
            if (targetItem) {
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (!sourceIndex.isValid() && sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                }

                if (sourceIndex.isValid()) {
```
`sourceIndex` is the index of the file in VFS.
```cpp
                    if (sourceView == vfsExplorer) {
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
`draggedItem` is the file in the tree.
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If `draggedItem` is found, `moveItemToFolder` runs!
In `moveItemToFolder`:
```cpp
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
It returns `false` if the books are different.
"I have 1 folder 'Folder' and 1 file 'File' in Documents"
They are in the SAME book!
```cpp
    QString itemType = draggedItem->data(Qt::UserRole + 1).toString();
    int itemId = draggedItem->data(Qt::UserRole).toInt();
    QString targetType = targetItem->data(Qt::UserRole + 1).toString();
    int targetFolderId = targetItem->data(Qt::UserRole).toInt();

    if (draggedItem == targetItem) return false;
```
Not the same.
```cpp
    if (!targetType.endsWith("_folder")) {
        // skipped because it's docs_folder
    }
```
Skipped.
```cpp
    bool compatible = false;
    QString table;
    // ...
    else if (itemType == "document" && targetType == "docs_folder") {
        table = "documents";
        compatible = true;
    }
```
`compatible` is true. `table` is "documents".
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
`db->moveItem` returns true.
`takeRow` removes it from `parent`.
`appendRow` adds it to `targetItem`.
Returns `true`.

If it returns `true`, then `dropEvent->acceptProposedAction()` is called!
And `return true;` is called in the event filter!

IF ALL THIS RUNS, WHY DOES IT NOT DO ANYTHING?!
Wait! Does `targetItem->appendRow(taken)` ACTUALLY move it visually?!
If you have a tree view, and you do `takeRow` and `appendRow`...
`QTreeView` sees `rowsRemoved` on the old parent, and `rowsInserted` on the new parent.
Does it update the UI? YES!
But wait! If `draggedItem` is currently selected in `vfsExplorer`, and you `takeRow` from the tree...
`rowsRemoved` is triggered.
`refreshVfsExplorer()` is triggered because `parent == currentIdx`!
`vfsExplorer` is CLEARED!
`QListView` selection model loses the selection!
The drag is STILL HAPPENING (we are in the `Drop` event)!
If the selection is lost while `QDrag` is finalizing...
Does it crash? No. Does it abort? Maybe!
If `QDrag` aborts, what happens?
It just returns. But `takeRow` ALREADY HAPPENED!
If `takeRow` and `appendRow` already happened, the tree view MUST show the file inside the folder!
If the tree view DOES NOT show the file inside the folder... then this code DID NOT RUN!
OR it ran, and then it got REVERTED?!
Why would it get reverted?!
Could `db->moveItem` be FAILING?!
"1 folder 'Folder' and 1 file 'File' in Documents"
Did the user click "Save Document" to save the file?!
"I take a file from the file explorer"
If it's in the file explorer, it has a name "File".
If it has a name, it was saved! (Or created via "New Document" and then renamed via Breadcrumbs?)
Wait! If it was renamed via Breadcrumbs...
```cpp
            connect(edit, &QLineEdit::returnPressed, this, [this, edit, treeItem, type]() {
                QString newName = edit->text().trimmed();
                // ...
                int id = treeItem->data(Qt::UserRole).toInt();

                if (id != 0) {
                    if (type == "document") currentDb->updateDocument(id, newName, documentEditorView->toPlainText());
```
If `id` == 0, it DOES NOT save it! It just changes `treeItem->setText(newName)`!
If `id` == 0, `db->moveItem` tries to update `id = 0`!
It matches 0 rows! It returns `true` (`SQLITE_DONE`)!
`takeRow` and `appendRow` run!
It SHOULD visually update!
Why doesn't it visually update?!
Wait! I bet it DOES visually update!
But the user says "it doesn't do anything"!
Why would they say that?
Maybe it REALLY doesn't do anything because `dropEvent->acceptProposedAction()` followed by `return true` means the NATIVE drop is blocked...
Wait, if the native drop is blocked, and my manual drop happens...
Is there a difference?
Native drop: Qt handles everything.
Manual drop: `takeRow` and `appendRow`.
If `takeRow` and `appendRow` work, they show exactly what a native drop shows!
Except the expanded state of the source folder is preserved.

Wait! Could it be that `eventFilter` is returning `false`?
Let's look at `DragMove`.
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
I returned `false` for `DragMove` AND `DragEnter`!
Wait! In `DragEnter`, I added:
```cpp
    if (event->type() == QEvent::DragEnter) {
        QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);

        QWidget* sourceWidget = qobject_cast<QWidget*>(dragEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }

        if (dragEvent->mimeData()->hasUrls()) {
            dragEvent->acceptProposedAction();
            return true;
        }
        if ((obj == openBooksTree || obj == openBooksTree->viewport()) && dragEvent->source() == bookList) {
            dragEvent->acceptProposedAction();
            return true;
        }
        if ((obj == bookList || obj == bookList->viewport()) && sourceView == openBooksTree) {
            dragEvent->acceptProposedAction();
            return true;
        }
        if (sourceView == openBooksTree || sourceView == vfsExplorer) {
            return false; // let native handling show drop indicator
        }
    }
```
If I return `false` for `DragEnter` and `DragMove`, I am letting the `QTreeView` natively process them!
Does `QTreeView` natively accept them?!
Yes, because `ItemIsDropEnabled` is `true`.
So it DOES show the drop indicator.
Then `Drop` event is fired!
`Drop` event is caught by `eventFilter`.
It returns `true`.
It calls `takeRow` and `appendRow`.

Is there ANY chance that `takeRow` and `appendRow` are not doing what they should?!
In `QStandardItemModel`, `parent->takeRow(row)` returns a `QList<QStandardItem*>`.
If you pass this list to `targetItem->appendRow(taken)`, it adds it to the target item!
This is literally the standard way to move items in a `QStandardItemModel`!
It MUST work!
Why does the user say it doesn't do anything?!
"when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"

Wait... I just saw something!
If `moveItemToFolder` returns `true`, it calls `dropEvent->acceptProposedAction();`!
BUT what if it returns `false`?!
If it returns `false`, it calls `dropEvent->ignore();`!
Is it returning `false`?!
Let's check `compatible` again!
```cpp
    bool compatible = false;
    QString table;
    if (itemType == "chat_session" && targetType == "chats_folder") {
        table = "messages";
        compatible = true;
    } else if (itemType == "document" && targetType == "docs_folder") {
        table = "documents";
        compatible = true;
    }
```
What if `targetType` is NOT "docs_folder"?!
What if `targetType` is "document"?!
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
If you drag a file and drop it BETWEEN rows!
If you drop BETWEEN rows, `indexAt` is INVALID!
```cpp
            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
            QStandardItem* targetItem = nullptr;

            if (targetIndex.isValid()) {
```
If `targetIndex` is invalid, `targetItem` is `nullptr`!
If `targetItem` is `nullptr`, the `if (targetItem)` block is SKIPPED!
And it falls through to:
```cpp
            }
            dropEvent->ignore();
            return true;
```
If it falls through to `dropEvent->ignore()`, it DOES NOTHING!
YES! THIS IS IT!
The user is dragging the file and dropping it BETWEEN folders in the tree view!
If you drop a file onto a folder, it goes INSIDE the folder.
If you drop a file BETWEEN folders, it goes into the PARENT folder of those folders!
Because native Qt draws a line between the folders, the user thinks "I am dropping it between these two folders, which means I am moving it out of its current folder and into the main Documents folder".
BUT because `targetIndex` is invalid (it's between rows), `targetItem` is `nullptr`!
And my code `return true;` blocks it!
AND IT DOES NOTHING!
Yes!!!
"when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
Wait, "in a folder" could mean "inside the folder's children list"!
If they expand the folder, and drop the file INTO the list of children, they are dropping it BETWEEN the children!
If they drop it BETWEEN the children, `targetIndex` is INVALID!
Because it's not ON a child! It's BETWEEN children!
If it's BETWEEN children, it's INVALID!
So `targetItem` is `nullptr`!
And it DOES NOTHING!
THIS IS IT!!!
The user is not dropping the file ON the folder icon! They are expanding the folder, and dropping the file into the empty space or between the items inside the folder!
If you drop between items, `QTreeView` natively handles it by inserting the row at that position!
But my code intercepts it, sees `targetIndex` is invalid, and ABORTS!
Wow. That explains EVERYTHING!

How to fix it?!
If `targetIndex` is invalid, we can get the drop target parent from `QTreeView::dropIndicatorPosition()` or by examining the `parent` of the items around it!
Actually, `QDropEvent` doesn't provide this easily.
But `QTreeView::indexAt(pos)` might return invalid, BUT `QAbstractItemView::dropIndicatorPosition()` tells us where it's dropping!
Actually, there is a much easier way:
If `targetIndex` is invalid, check if the tree has a current drop parent!
Wait, `QDropEvent` doesn't know the drop parent.
BUT we can just ask `openBooksTree->indexAt(pos)` but with a slight offset?
No! If we just let the NATIVE drop happen, we can catch the `rowsInserted` signal!
Wait! I said earlier:
"If we let the native drop happen, `QTreeView` handles dropping between items, ON items, etc! And it updates the model natively! And then we can hook into `rowsInserted`!"
Yes! But I said we can't because `loadDocumentsAndNotes` triggers `rowsInserted`!
But we CAN use a boolean flag `m_isInternalMove`!
When `Drop` happens, we set `m_isInternalMove = true`, let the NATIVE drop happen (`return false`), and in `rowsInserted`, we check `m_isInternalMove`!
BUT `Drop` event returns immediately, so when does the native drop finish?
It finishes immediately after `Drop`!
So we can just use a `QTimer::singleShot(0)` to reset `m_isInternalMove = false`!
Wait! Even better! We don't need `m_isInternalMove`.
In `QEvent::Drop`, we can just GET the target parent!
How? `QTreeView` has a protected method `dropEvent`. It calculates the parent.
We can't access it easily.
But wait! If they drop between items, `targetIndex` is invalid, BUT we can just find the nearest item!
If `pos.y() > 0`, we can iterate `y` up or down until `indexAt(y)` is valid!
If we find a valid index nearby, we take its parent!
```cpp
            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
            if (!targetIndex.isValid()) {
                // Find nearest item above
                for (int y = dropEvent->position().toPoint().y(); y >= 0; --y) {
                    targetIndex = targetView->indexAt(QPoint(dropEvent->position().toPoint().x(), y));
                    if (targetIndex.isValid()) break;
                }
                // If found, the drop target is the PARENT of this item (since it's dropped between items)
                // Actually, if you drop below a folder, is it inside the folder or below the folder?
            }
```
This is too hacky.
Is there ANY way to get the drop target in `QTreeView`?
Yes! `openBooksTree->indexAt(pos)` IS the item. If it's between items, there is a way:
Actually, `QAbstractItemModel::dropMimeData` is where the drop actually happens!
We can subclass `QStandardItemModel` and override `dropMimeData`!
BUT we didn't subclass `QStandardItemModel`.

Wait, what if we JUST let native drop happen, but intercept `QAbstractItemModel::rowsInserted`?!
If we only intercept `rowsInserted` when `mainContentStack->currentWidget() == vfsExplorer`? No, we need to update the DB!
Can we update the DB inside `QStandardItemModel::itemChanged`?
When an item is moved, does `itemChanged` fire? No.

Wait. Is it REALLY dropping between rows?
"when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
If they drop it IN a folder. Not BETWEEN rows!
If they drop it ON the folder, `targetIndex` IS VALID.
If they drop it ON the folder, why would it not work?

Let's assume my `DragMove` returning `false` fixed the tree drop indicator, but they are dropping ON a folder, and it still fails.
Why would it fail if `targetIndex` is valid?
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (!sourceIndex.isValid() && sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                }
```
If `sourceIndex` is NOT valid?
When dragging from VFS Explorer to Tree View, the `sourceView` is VFS.
Does VFS Explorer maintain its `currentIndex()` or `selectionModel()->hasSelection()`?
Yes!

Wait! I see it!
```cpp
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (sourceView == vfsExplorer) {
                        QStandardItem* vfsItem = vfsModel->itemFromIndex(sourceIndex);
```
If you drag from VFS Explorer, `sourceView` IS `vfsExplorer`.
BUT in my latest patch:
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
```
This is correct.
Wait, I found the bug!
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If `moveItemToFolder` returns `true`, it calls `dropEvent->acceptProposedAction()`.
BUT IT DOES NOT CALL `loadDocumentsAndNotes()`!
Because in Plan 45, I REMOVED `loadDocumentsAndNotes()` from `moveItemToFolder`!
```cpp
    if (itemType.endsWith("_folder") && targetType == itemType) {
        if (db->moveFolder(itemId, targetFolderId)) {
            QStandardItem* parent = draggedItem->parent();
            if (parent) {
                QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
                targetItem->appendRow(taken);
            }
            return true;
        }
    }

    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
        return true;
    }
```
I did this:
```cpp
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
```
If I drag from VFS Explorer to Tree View:
`draggedItem` is the TREE item! (Found via `findItemRecursive`).
`targetItem` is the TREE folder!
I `takeRow` from the TREE parent, and `appendRow` to the TREE target!
This updates the TREE!
But does it update the VFS Explorer?!
Wait! `parent->takeRow` triggers `rowsRemoved` on the TREE!
`targetItem->appendRow` triggers `rowsInserted` on the TREE!
If the VFS Explorer is viewing the `parent` folder, it sees `rowsRemoved` and calls `refreshVfsExplorer()`!
If `refreshVfsExplorer()` clears the VFS model, what happens to `vfsExplorer`?
It clears! So the file disappears from VFS!
This is PERFECT!
But wait! Does it crash because `QDrag` is still active?
If `takeRow` happens during `QEvent::Drop`, Qt's drag-and-drop system might not expect the model to change under its feet!
But it usually handles it fine.
If it handles it fine, why does the user say "it doesn't do anything"?!

Is there a chance `draggedItem->parent()` is `nullptr`?!
If `draggedItem` is a book, it's `nullptr`. But we can't drag books.
If `draggedItem` is a chat_session, documents, notes... they are all under root folders (e.g. `docs_folder`).
The root folders are under the book.
So `parent` is NEVER `nullptr`!

What if `compatible` is `false`?
If `itemType` = "document" and `targetType` = "docs_folder", `compatible` is `true`.

Wait! What if they dragged a folder?
If they dragged a folder, `itemType` is "docs_folder", `targetType` is "docs_folder".
It enters `else if (itemType.endsWith("_folder") && targetType == itemType)`!
It updates DB, takes row, appends row. Returns true.

I am absolutely certain my code is flawless EXCEPT if `QTreeView` is somehow eating the drop event or if `targetIndex` is invalid.
If `targetIndex` is invalid (dropping between rows), it DOES NOTHING. This is the MOST LIKELY REASON a user would say "it doesn't do anything"!
When users drag files into folders in a tree view, they often just drag it to the general area under the folder. If they don't hit the folder text exactly, `targetIndex` is invalid!
If it's invalid, my code `return true;` swallows the event!
If I just LET THE NATIVE DROP HAPPEN instead of swallowing it when `targetIndex` is invalid:
```cpp
            if (targetIndex.isValid()) {
                // ... my code ...
                return true;
            }
            // If invalid, let native drop handle it?!
            // But if native drop handles it, it won't update the DB!
```
Wait! If native drop handles it, we CAN intercept `rowsInserted`!
Let's just change `moveItemToFolder` to BE CALLED from `rowsInserted`?!
No, `rowsInserted` doesn't give us the dragged item.
But wait! `QStandardItemModel::itemChanged`? No.

Wait. Is it possible that `targetItem->appendRow(taken)` DOES NOT WORK because `taken` is empty?!
Let's check `QStandardItem::takeRow`:
```cpp
QList<QStandardItem*> QStandardItem::takeRow(int row)
```
Does it return the items? Yes.
Does `appendRow` add them? Yes.
Is there ANY chance `takeRow` returns an empty list?
If `draggedItem->row()` is wrong? No, `row()` returns the row.

What if we just use `QTimer::singleShot` to defer the DB update and model manipulation?
If we defer it, the drag-and-drop finishes normally.
But if we return `true`, native drop is blocked anyway.
So deferring it wouldn't change anything.

Let's look at the user's EXACT words:
"When I move something in the file view the tree view completely refreshes which causes it to forget which nodes were open / collapsed, which is disorientating to the users it should be an update operation not a complete refresh of the data"
I FIXED THIS in plan 45 (fix_moves.patch)! I replaced `loadDocumentsAndNotes` with `takeRow`/`appendRow`!
The user replied:
"the sync now works thanks, and it updates, however drag drop in the tree view isn't working. when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"
SO MY FIX IN PLAN 45 CAUSED DRAG DROP IN THE TREE VIEW TO STOP WORKING!
BEFORE plan 45, drag drop in the tree view WORKED! (But it rebuilt the tree).
AFTER plan 45, it STOPPED WORKING! ("it doesn't do anything").
WHY did plan 45 break it?!
Let's look at what plan 45 changed in `moveItemToFolder`:
```cpp
<<<<<<< SEARCH
    if (itemType.endsWith("_folder") && targetType == itemType) {
        if (db->moveFolder(itemId, targetFolderId)) {
            loadDocumentsAndNotes();
            return true;
        }
    }

    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        loadDocumentsAndNotes();  // This should be updated for the specific book if needed, but for now it's okay
        return true;
    }
=======
    if (itemType.endsWith("_folder") && targetType == itemType) {
        if (db->moveFolder(itemId, targetFolderId)) {
            QStandardItem* parent = draggedItem->parent();
            if (parent) {
                QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
                targetItem->appendRow(taken);
            }
            return true;
        }
    }

    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
        return true;
    }
>>>>>>> REPLACE
```
THIS IS THE ONLY THING THAT CHANGED!
If changing `loadDocumentsAndNotes()` to `takeRow`/`appendRow` BROKE IT...
Then `takeRow`/`appendRow` MUST BE SILENTLY FAILING! OR CRASHING! OR ABORTING THE DROP!
Why would `takeRow` and `appendRow` fail?!
Because `draggedItem` is currently BEING DRAGGED!
If you `takeRow` on an item that is currently the source of a drag operation, the underlying Qt model might abort the operation or it might become invalid!
Wait! The drag operation holds a reference to the `QModelIndex` of the source item!
If you `takeRow`, the `QModelIndex` becomes invalid!
If the `QModelIndex` becomes invalid, the drag operation might abort!
Or `takeRow` might just not work correctly during a drop event?
No, it removes the item. But `QTreeView` might be drawing the drop indicator based on the item, and deleting it causes issues.
Wait, if it was VFS to VFS, it worked?!
"the sync now works thanks, and it updates"
In VFS to VFS, the dragged item is in the TREE!
`takeRow` removes it from the tree. The VFS is updated. It works!
BUT in Tree to Tree, the dragged item is IN THE TREE!
And the drag source is THE TREE!
If we `takeRow` on the drag source, the drag source model index becomes invalid!
And Qt might ABORT the drop!
YES!

How can we fix it?!
Instead of `takeRow` and `appendRow` IMMEDIATELY, we can defer it!
Or, instead of `takeRow`, we can let the NATIVE drop happen, but update the DB?!
Wait! If we return `true`, the native drop NEVER HAPPENS!
If the native drop never happens, Qt expects US to move it!
If we defer it:
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
If we defer it, the `Drop` event returns `true` IMMEDIATELY.
The drag operation finishes safely!
THEN the event loop runs the timer!
THEN `takeRow` and `appendRow` are called safely!
THIS IS IT!
This completely explains why VFS to VFS worked (the drag source was the VFS, not the Tree, so taking the Tree row didn't invalidate the VFS drag source!), but Tree to Tree failed!
And it explains why VFS to Tree failed! (Because VFS to Tree ALSO takes the Tree row? Wait, if VFS to Tree takes the Tree row, the drag source is VFS! It shouldn't invalidate the VFS drag source! But maybe it triggers `refreshVfsExplorer()` which clears the VFS model, invalidating the VFS drag source!)
Yes! If `refreshVfsExplorer()` clears the VFS model during the drop event, the VFS drag source is invalidated, causing it to abort!
If we defer the `takeRow`, BOTH problems are solved!

Let's implement the `QTimer::singleShot`!
