Wait! If `DragEnter` and `DragMove` return `false`, they are passed to the `QTreeView`.
The `QTreeView` natively accepts them if `targetIndex` is a valid drop target (e.g. a folder).
If `targetIndex` is NOT a valid drop target (e.g. a file, since our model doesn't support dropping into a file), the native `QTreeView` will REJECT the drop (`DragMove` ignored)!
If the native `QTreeView` rejects the drop, it shows the forbidden cursor!
And if the user drops the file anyway, the `Drop` event will NOT BE FIRED by Qt!
Because Qt only fires `Drop` if `DragMove` was accepted!
Ah!!!
This means we CANNOT rely on native `QTreeView` to accept drops over files! Because natively, files do NOT accept drops!
Unless we modify `BookDatabase::flags()` or the `QStandardItemModel` to make files `ItemIsDropEnabled`!
Wait! `QStandardItemModel` assigns `ItemIsDropEnabled` to items by default?
Yes! `QStandardItem::setFlags` includes `ItemIsDropEnabled` by default!
If files have `ItemIsDropEnabled`, the native `QTreeView` WILL accept drops over files!
And it will draw the drop indicator ON the file!
And when dropped, it will make the dropped item a CHILD of the file!
Wait! If it becomes a CHILD of the file, `QTreeView` natively accepts it.
BUT in our `QEvent::Drop` handler, we INTERCEPT it!
If we intercept it, we get the `Drop` event!
And we do:
```cpp
    if (!targetType.endsWith("_folder")) {
        if (targetItem->parent()) {
            targetItem = targetItem->parent();
```
We redirect it to the parent folder!
So if we let `QTreeView` handle `DragMove` natively, it WILL accept drops over files (because `ItemIsDropEnabled` is true), and it WILL show the drop indicator, and it WILL fire `Drop`!
Let's check if `QStandardItem` has `ItemIsDropEnabled`.
Yes, default flags are `ItemIsSelectable | ItemIsEnabled | ItemIsEditable | ItemIsDragEnabled | ItemIsDropEnabled`.
So it SHOULD work natively!

BUT WAIT! If it works natively, why did the user say "tree view drag drop works" initially?
Because BEFORE my patch, they used native drops, and it visually moved!
But wait, if native drop puts the item INSIDE the file (as a child), that's visually wrong!
But they said "tree view drag drop works". So maybe they dropped it ON A FOLDER.

What if they drop BETWEEN rows?
Native `QTreeView` accepts drops BETWEEN rows because `QStandardItemModel` supports reordering!
If it drops BETWEEN rows, our `QEvent::Drop` handler will see `targetIndex.isValid() == false`!
Wait! `targetView->indexAt(dropEvent->position().toPoint())` returns INVALID if it's BETWEEN rows!
If it returns INVALID, `targetItem` is `nullptr`.
If `targetItem` is `nullptr`, my custom `Drop` handler reaches `dropEvent->ignore(); return true;`!
Which BLOCKS the drop!
So dropping BETWEEN rows will NOT WORK!
And it SHOULDN'T work! Because SQLite doesn't support reordering for files!
So blocking it is CORRECT!
But if they drop ON a folder, `targetItem` is the folder. It works!
If they drop ON a file, `targetItem` is the file. It redirects to the parent folder. It works!

Wait... "when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything."
If they drop it ON a folder, `targetIndex` IS VALID.
`moveItemToFolder` is called.
`targetItem->appendRow(taken)` is called.
Why did the user say "it doesn't do anything"?!
Wait, could `sourceIndex` be INVALID?!
In `Drop` event:
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (sourceIndex.isValid()) {
```
If `QListView` does NOT set `currentIndex()` during a drag?!
Yes! If you click and drag in `QListView`, does it set `currentIndex`?
In Qt, `QListView` only sets `currentIndex` if you click WITHOUT dragging?
Actually, `QAbstractItemView` sets selection on press.
Let's change it to check `selectedIndexes()`.
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (!sourceIndex.isValid() && sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                }
                if (!sourceIndex.isValid()) {
                    QModelIndexList selected = sourceView->selectionModel()->selectedIndexes();
                    if (!selected.isEmpty()) sourceIndex = selected.first();
                }
```
Let's make sure we find the `sourceIndex`!
AHA!!!
```cpp
            return false; // let native handling show drop indicator
```
I changed `eventFilter` to `return false` for `QEvent::DragMove`!
If I return `false`, I am telling the event filter "I did not handle this event, pass it to the original receiver".
The original receiver is `QTreeView::viewport()`.
`QAbstractScrollArea` redirects viewport events to `viewportEvent()`, which then calls `dragMoveEvent()`.
`QTreeView::dragMoveEvent()` is called!
Does `QTreeView::dragMoveEvent()` accept the drop?
YES, if the target is valid according to the model.
Our model is `QStandardItemModel`. Does it accept drops?
Yes, `QStandardItem` has `ItemIsDropEnabled` by default.
So it DOES accept the drop!

BUT WAIT! If `DragMove` is passed to the original receiver, what about `Drop`?!
In `QEvent::Drop`, I DO `return true;`!
If I `return true;`, I consume the event!
BUT wait! Does `QTreeView` NEED to receive the `Drop` event to complete its internal drag-and-drop state machine?!
If I `return true;`, `QTreeView` NEVER gets the `Drop` event!
If `QTreeView` NEVER gets the `Drop` event, its internal drag state is NEVER finished properly?!
Wait! `QDrag::exec()` is running the event loop.
When a `Drop` event is processed, if the widget calls `acceptProposedAction()`, `QDrag` knows it succeeded.
I am calling `dropEvent->acceptProposedAction()` inside `eventFilter`!
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If I call `acceptProposedAction()`, `QDrag` WILL return `Qt::MoveAction`!
Wait! If `QDrag` returns `Qt::MoveAction`, the SOURCE view (which initiated the drag) MIGHT NATIVELY DELETE THE ITEM!
Ah!!!!!!!
If `sourceView` is `openBooksTree`, and `QDrag::exec` returns `Qt::MoveAction`, the standard `QAbstractItemView` logic WILL DELETE THE DRAGGED ITEM from the source model!!!
Wait! If it deletes the dragged item from the source model, BUT I ALREADY manually moved it:
```cpp
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
```
If I manually moved it, it is no longer at `sourceIndex`!
Wait, when `QAbstractItemView` deletes the source item, it uses the indices it saved when the drag started!
If those indices are invalid, it might crash? Or if it deletes the item, it might delete the WRONG item, or the moved item?!
Wait, if it deletes it, the item DISAPPEARS!
"when I take a file from the file explorer and drop it in a folder in the tree view it doesn't do anything. It also doesn't do anything when I drop a file from the tree view onto a folder in the tree view"
If it "doesn't do anything", it means it's NOT DELETED either!
So the native view is NOT deleting it.
Why is the native view not deleting it?
Maybe `dropEvent->acceptProposedAction()` is NOT enough to signal the native view?
Actually, `QAbstractItemView` expects the drop target to handle the item addition, and if it's a move, the source view removes it.
But if the target view returns `true` from the event filter, it prevents the target view from handling it.

Wait, why does it NOT visually move?
If I do `takeRow` and `appendRow`, the item is moved.
Why would it NOT visually move?
Could `compatible` be FALSE?!
Let's add a test:
If I drop a file on a folder, `itemType` is "document". `targetType` is "docs_folder".
Wait! `findItemRecursive` returns the folder!
But what if `itemType` is WRONG?
What if `draggedItem` is a `vfsItem` and I am looking for it in the tree, but the types are slightly different?
In VFS: `itemType` is "document".
In Tree: `itemType` is "document".
It should match!

Is there any chance that `openBooksTree->viewport()->installEventFilter(this)` is WRONG?
What if `obj` is NOT `openBooksTree->viewport()`?!
Wait. If `obj` is the viewport, `eventFilter` catches it.
But what if `dropEvent->source()` is NOT `openBooksTree` OR `vfsExplorer`?
If `dropEvent->source()` is `openBooksTree->viewport()`, I added:
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }
```
If `sourceWidget` is `openBooksTree->viewport()`, `sourceWidget->parentWidget()` is `openBooksTree`.
`qobject_cast` to `QAbstractItemView*` WILL SUCCEED!
So `sourceView` IS `openBooksTree`!

So `sourceView` IS matched. `targetView` IS matched.

Wait! I see it!
```cpp
                    if (sourceView == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else if (sourceView == vfsExplorer) {
```
What if `sourceIndex` is INVALID?!
```cpp
                QModelIndex sourceIndex = sourceView->currentIndex();
                if (!sourceIndex.isValid() && sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                }
```
If `sourceIndex` is STILL INVALID?
Then `sourceIndex.isValid()` is false. It falls through to `dropEvent->ignore(); return true;`.
Is it possible `sourceIndex` is invalid?
When you drag an item, `selectionModel()->hasSelection()` SHOULD be true.
BUT what if it's NOT true?
Why wouldn't it be true?
What if `QTreeView` clears the selection before `Drop` is called?
No, it doesn't.

What if `moveItemToFolder` is returning `false`?!
Let's review `moveItemToFolder` carefully.
```cpp
    QStandardItem* sourceBook = draggedItem;
    while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();
    QStandardItem* targetBook = targetItem;
    while (targetBook && targetBook->parent()) targetBook = targetBook->parent();

    if (!sourceBook || !targetBook || sourceBook != targetBook) return false;
```
If dragging from tree to tree, `draggedItem` and `targetItem` are both from `openBooksModel`.
`sourceBook` and `targetBook` will BOTH be the root items.
Wait!!!
What if `sourceBook != targetBook`?!
When I load books:
```cpp
    QStandardItem* bookItem = new QStandardItem(QIcon::fromTheme("application-x-sqlite3"), fileName);
    // ...
    openBooksModel->appendRow(bookItem);
```
If I have 1 book open, `sourceBook` is `bookItem`. `targetBook` is `bookItem`.
They are equal pointers! `sourceBook != targetBook` is FALSE. So it proceeds!

```cpp
    QString itemType = draggedItem->data(Qt::UserRole + 1).toString();
    int itemId = draggedItem->data(Qt::UserRole).toInt();
    QString targetType = targetItem->data(Qt::UserRole + 1).toString();
    int targetFolderId = targetItem->data(Qt::UserRole).toInt();
```
`itemType` = "document". `itemId` = valid.
`targetType` = "docs_folder". `targetFolderId` = valid.

```cpp
    if (draggedItem == targetItem) return false;
```
Not equal.

```cpp
    if (!targetType.endsWith("_folder")) {
```
It IS "_folder", so skips.

```cpp
    bool compatible = false;
    QString table;
    // ...
    else if (itemType == "document" && targetType == "docs_folder") {
        table = "documents";
        compatible = true;
    }
```
`compatible` = true.

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
`parent` = "Documents" folder.
`parent->takeRow` removes the file from "Documents".
`targetItem` = "Folder".
`targetItem->appendRow` adds the file to "Folder".
It returns `true`.

IF THIS ALL HAPPENS, THE TREE VISUALLY UPDATES!
But the user says "it doesn't do anything".
This implies THIS DID NOT HAPPEN!
Which implies ONE OF THE CONDITIONS FAILED!

Could it be `targetIndex.isValid()` is FALSE?
If the user drops it on the folder, but the `QTreeView` doesn't resolve the index correctly?!
```cpp
            QModelIndex targetIndex = targetView->indexAt(dropEvent->position().toPoint());
```
If the user drops it on the folder, `indexAt` returns the folder.
Wait! `QDropEvent::position()` returns a `QPointF`. `toPoint()` returns `QPoint`.
What if `indexAt` returns an invalid index if you drop slightly off center?
If `targetIndex.isValid()` is false:
```cpp
            if (targetIndex.isValid()) {
                // ...
            } else if (targetView == vfsExplorer) {
                // ...
            }

            if (targetItem) {
                // ...
            }
            dropEvent->ignore();
            return true;
```
If it's false, `targetItem` is `nullptr` (since initialized to `nullptr`).
It falls through to `dropEvent->ignore(); return true;`!
AND DOES NOTHING!
If `indexAt` is returning invalid often, the user will experience "it doesn't do anything" ALMOST EVERY TIME!
Why would `indexAt` return invalid often?
Because `QTreeView` supports dropping BETWEEN items!
If you drop between items, `indexAt` returns INVALID!
BUT the visual drop indicator SHOWS a line between items! So the user thinks it's a valid drop!
If the user drops it when the line is shown, `indexAt` returns INVALID!
And our code does NOTHING!
AND the native drop is BLOCKED!
So "it doesn't do anything"!
YES!!!!!

How do we handle dropping between items?!
If the user drops between items, they want to drop it into the PARENT folder of those items!
If `targetIndex` is invalid, but the drop indicator was shown, we should find out the parent!
Actually, `QTreeView` provides `QAbstractItemView::dropIndicatorPosition()` but it doesn't provide the parent index directly in the event!
Wait! The simplest solution: JUST LET THE NATIVE DROP HAPPEN!
If we let the native drop happen, `QTreeView` handles dropping between items, ON items, etc!
And it updates the model natively!
BUT if we let native drop happen, HOW do we update the database?!
We can connect to `rowsInserted` on `openBooksModel`!
```cpp
    connect(openBooksModel, &QStandardItemModel::rowsInserted, this, [this](const QModelIndex& parent, int first, int last) {
        // ...
    });
```
Is it safe?
When dragging within the tree, `QTreeView` calls `moveRows` or `insertRows`/`removeRows`.
If it calls `rowsInserted`, we can get the item: `openBooksModel->itemFromIndex(openBooksModel->index(first, 0, parent))`.
We get its ID: `item->data(Qt::UserRole).toInt()`.
We get the parent folder's ID: `openBooksModel->itemFromIndex(parent)->data(Qt::UserRole).toInt()`.
And we update the database!
BUT wait! When we load books, `populateDocumentFolders` ALSO triggers `rowsInserted`!
We don't want to update the DB during load!
We can set a flag `bool m_isLoading = false;`.
If `m_isLoading`, we ignore `rowsInserted`!
This would be SO MUCH SIMPLER and support ALL native drop features!

Wait! Does `vfsExplorer` native drop work the same way?
`vfsExplorer` is a `QListView`. It's a flat list. If you drop an item, it reorders it in the flat list. It DOES NOT change its parent! Because a flat list has no parents!
So native drop in `vfsExplorer` DOES NOT WORK for moving into folders!
Because `QListView` doesn't know that "Folder" is a container! It just thinks "Folder" is an item in the list!
So for `vfsExplorer`, we MUST intercept it and manually move it!
For `openBooksTree`, we could let it handle natively, but `openBooksTree` might not know that you can't move a document into a chats_folder! Natively, `QStandardItemModel` allows moving ANY item to ANY parent!
So if we let it happen natively, the user could drop a document into a chats_folder!
Then `rowsInserted` fires. We check if it's valid. If not, we have to UNDO the native drop?! That's terrible!

So we MUST intercept it!
If we must intercept it, how do we handle `targetIndex` being invalid?
If `targetIndex` is invalid, the user dropped it in the empty space or between items.
If they drop it between items in `QTreeView`, how do we know WHICH parent folder they dropped it in?
Wait! Does the user say "when I drop a file from the tree view onto a folder in the tree view it doesn't do anything"?
If they drop it ONTO a folder, `indexAt` IS VALID!
If it's valid, it shouldn't fail!
Why would it fail if `targetIndex` is valid?!

Let's rethink `sourceIndex`.
What if `sourceView` is NOT `openBooksTree`?
What if `dropEvent->source()` is `openBooksTree`, but `sourceWidget` is `openBooksTree->viewport()`?
```cpp
        QWidget* sourceWidget = qobject_cast<QWidget*>(dropEvent->source());
        QAbstractItemView* sourceView = qobject_cast<QAbstractItemView*>(sourceWidget);
        if (!sourceView && sourceWidget && sourceWidget->parentWidget()) {
            sourceView = qobject_cast<QAbstractItemView*>(sourceWidget->parentWidget());
        }
```
If `dropEvent->source()` is `openBooksTree->viewport()`, `sourceView` becomes `openBooksTree`.
This IS correct.

Wait, what if `moveItemToFolder` fails because `compatible` is FALSE?!
"1 folder 'Folder' and 1 file 'File' in Documents"
`itemType` = "document". `targetType` = "docs_folder".
Is `targetType` REALLY "docs_folder"?
When the user created "Folder" inside "Documents":
```cpp
            int newFId = currentDb->addFolder(parentId, name, fType);
            QStandardItem* fItem = new QStandardItem(QIcon::fromTheme("folder-open"), name);
            fItem->setData(newFId, Qt::UserRole);
            fItem->setData(type, Qt::UserRole + 1);
            item->appendRow(fItem);
```
What is `type`?
`QString type = item->data(Qt::UserRole + 1).toString();`
`item` is the parent folder ("Documents").
"Documents" has type `"docs_folder"`!
So `fItem` gets type `"docs_folder"`!
This is perfectly correct.

What if `itemId` is wrong?
`int itemId = draggedItem->data(Qt::UserRole).toInt();`
When the file is created:
```cpp
        if (isCreatingNewDoc && item) {
            // ...
            int newId = 0;
            QString type = item->data(Qt::UserRole + 1).toString();
            if (type == "document") newId = currentDb->addDocument(folderId, currentTitle, documentEditorView->toPlainText());
            // ...
            item->setData(newId, Qt::UserRole);
            item->setData(type, Qt::UserRole + 1);
```
Wait! `item->setData(type, Qt::UserRole + 1);`
When creating a new document in "Documents" folder:
`item` is the PHANTOM item!
`type` is fetched from `item->data(Qt::UserRole + 1).toString();`.
What is the type of the phantom item?
In `addPhantomItem`:
```cpp
    QString itemType = type;
    if (type == "chats_folder") itemType = "chat_session";
    else if (type == "docs_folder") itemType = "document";
    // ...
    phantomItem->setData(itemType, Qt::UserRole + 1);
```
So the phantom item's type is `"document"`!
Then in `saveDocBtn`:
```cpp
            QString type = item->data(Qt::UserRole + 1).toString();
```
`type` is `"document"`.
```cpp
            if (type == "document") newId = currentDb->addDocument(...);
            // ...
            item->setData(newId, Qt::UserRole);
            item->setData(type, Qt::UserRole + 1);
```
It sets `Qt::UserRole + 1` to `"document"`.
So the item is a `"document"`.
This is ALSO perfectly correct!

Then why does it "not do anything"?!
Could it be that the user is trying to drag and drop multiple items? "when I take a file". Singular.

Is there any chance that `openBooksTree->currentIndex()` is WRONG when dragging?
Let's use `selectedIndexes().first()`!
```cpp
                QModelIndex sourceIndex;
                if (sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                } else {
                    sourceIndex = sourceView->currentIndex();
                }
```
If I do this, I GUARANTEE I get the dragged item!
Because dragging ALWAYS requires a selection!

Is it possible that `db->moveItem` returns false because SQLCipher requires a transaction to be committed?
NO, auto-commit mode is the default in SQLite. Every statement is a transaction.

What if `moveItemToFolder` is NEVER called?!
Why would it never be called?
```cpp
            if (targetItem) {
                // ...
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
```
If `draggedItem` is NOT null, and `targetItem` is NOT null.
If `moveItemToFolder` returns `false`, `dropEvent->ignore()` is called, and `return true` happens.
If `moveItemToFolder` returns `true`, `dropEvent->acceptProposedAction()` is called, and `return true` happens.
If `dropEvent->acceptProposedAction()` is called, the drag and drop manager receives an ACCEPTED response, meaning the drop succeeded.
But since we `return true`, `QTreeView` NEVER receives the event!
Wait! IF `QTreeView` NEVER RECEIVES THE EVENT...
How does the `draggedItem` physically move in the UI?!
I added:
```cpp
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
        }
```
If `appendRow(taken)` executes, the item is VISUALLY MOVED in the `QStandardItemModel`.
Since `QTreeView` observes the model, it visually updates!
But wait... does `takeRow()` remove it from the model IMMEDIATELY?
Yes.
Does `appendRow()` add it IMMEDIATELY?
Yes.
If it visually updates, the user WOULD NOT SAY "it doesn't do anything"!
If they say "it doesn't do anything", IT MEANS THIS CODE IS NOT REACHED! OR `targetItem` is NULL! OR `draggedItem` is NULL! OR `moveItemToFolder` returns `false`!

Let's assume `targetItem` is NULL because the user drops BETWEEN rows.
If the user drops between rows, `indexAt` is invalid.
Can we fix dropping between rows?
If `targetIndex` is invalid, but we are inside `openBooksTree`...
Can we get the drop parent from `QTreeView`?
No, `QDropEvent` doesn't provide it.
BUT we can use `openBooksTree->indexAt(dropEvent->position().toPoint())`.
If the user drops directly on the folder text, it's valid.

Wait, look at `QEvent::DragMove`:
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
If we `return false`, the native handling shows the drop indicator!
When the user releases the mouse, `QEvent::Drop` fires.
Is it possible that the native handling ALREADY moved the item BEFORE `eventFilter` receives the `Drop` event?!
NO! `eventFilter` receives the `Drop` event BEFORE the object itself!

What if `sourceIndex` is INVALID?
I MUST fix `sourceIndex` extraction!
```cpp
                QModelIndex sourceIndex;
                if (sourceView->selectionModel()->hasSelection()) {
                    sourceIndex = sourceView->selectionModel()->selectedIndexes().first();
                } else {
                    sourceIndex = sourceView->currentIndex();
                }
```
Let's implement this!
