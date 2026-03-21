Okay, I need to clean up `MainWindow.cpp.orig`.
Wait, why is it still there? Let's rm it explicitly.

The user says: "I can drag and drop something on to a `..` and it doesn't seem to retain. Are we persisting the vfs changes?"
Wait! When you drop on `..`, what does `targetItem` resolve to?
In `eventFilter` for `QEvent::Drop`:
```cpp
                    QStandardItem* vfsItem = vfsModel->itemFromIndex(targetIndex);
                    if (vfsItem && vfsItem->text() == "..") {
                        QModelIndex treeIndex = openBooksTree->currentIndex();
                        if (treeIndex.isValid()) {
                            QStandardItem* currentFolder = openBooksModel->itemFromIndex(treeIndex);
                            if (currentFolder && currentFolder->parent()) targetItem = currentFolder->parent();
                        }
                    }
```
If `targetItem = currentFolder->parent()`, what does `moveItemToFolder` do?
```cpp
    QString targetType = targetItem->data(Qt::UserRole + 1).toString();
    int targetFolderId = targetItem->data(Qt::UserRole).toInt();

    // Handle dropping on an item (non-folder)
    if (!targetType.endsWith("_folder")) {
```
Wait! What if the parent is a BOOK item?
If `currentFolder` is `docs_folder`, its parent is the BOOK item.
If you drop something onto the BOOK item, `targetType` is `"book"`.
`targetType.endsWith("_folder")` is false!
So it goes inside:
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
If `targetItem` is the BOOK item, `targetItem->parent()` is `nullptr` (since books are root nodes in `openBooksTree`)!
So it hits `return false;`!
AND `moveItemToFolder` fails!
Wait, but you CANNOT move a document to the root of a book. Documents MUST be inside `docs_folder` or a subfolder of `docs_folder`.
Wait, what if `currentFolder` is a subfolder INSIDE `docs_folder`?
If `currentFolder` is "MySubFolder" (type `docs_folder`, parent `docs_folder`), then dropping on `..` makes `targetItem` the parent `docs_folder`.
If `targetItem` is `docs_folder`, `endsWith("_folder")` is TRUE.
So it continues:
```cpp
    bool compatible = false;
    QString table;
    if (itemType == "chat_session" && targetType == "chats_folder") { table = "messages"; compatible = true; }
    else if (itemType == "document" && targetType == "docs_folder") { table = "documents"; compatible = true; }
    ...
```
If it's compatible, it works!
Why does the user say "I can drag and drop something on to a `..` and it doesn't seem to retain"?
Maybe they tried to drop a Document onto `..` when they were inside the root `docs_folder`?
If they are inside `docs_folder`, `..` goes to the Book item. You cannot drop a document onto a Book item.
But wait! If they are inside `docs_folder`, is there a `..`?
Let's check `onOpenBooksSelectionChanged` or `refreshVfsExplorer`:
```cpp
    if (type != "book") {
        QStandardItem* upItem = new QStandardItem(QIcon::fromTheme("go-up"), "..");
        vfsModel->appendRow(upItem);
    }
```
Yes! Every folder except "book" has a `..`.
If they are in `docs_folder`, they see `..`. If they drop a document on `..`, it tries to move it to the Book item, which fails!
BUT `QListView::setDefaultDropAction(Qt::MoveAction)` makes it visually move anyway because the model is dragged-and-dropped automatically!
Wait! The `QListView` removes the item visually, but the database doesn't change! And since `loadDocumentsAndNotes()` wasn't called (because it returned `false`), the VFS is not refreshed, so it looks like it worked but it's permanently out of sync until a refresh happens!
Ah! `QEvent::Drop` returns `true` even if `moveItemToFolder` fails?
```cpp
                    if (draggedItem && moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                        return true;
                    }
```
If `moveItemToFolder` returns `false`, it falls through and returns `KXmlGuiWindow::eventFilter(obj, event);`.
Which might process the drop natively in the `QListView`!
To prevent `QListView` from visually processing the drop if it fails, we MUST handle it and `ignore()` the event, or just return `true` to consume it without doing anything!
Wait! If we drop onto `..` and it's invalid (e.g. moving a document to the Book root), we should visually reject it!
In `QEvent::DragMove`:
```cpp
        } else if (obj == vfsExplorer) {
            QModelIndex index = vfsExplorer->indexAt(dragEvent->position().toPoint());
            if (index.isValid()) {
                QStandardItem* targetItem = vfsModel->itemFromIndex(index);
                QString targetType = targetItem->data(Qt::UserRole + 1).toString();
                if (targetType.endsWith("_folder")) {
                    dragEvent->acceptProposedAction();
                    return true;
                }
            } else {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
```
Wait! `..` has no `targetType` set! Its `Qt::UserRole + 1` data is EMPTY!
So `targetType.endsWith("_folder")` is FALSE!
So `DragMove` DOES NOT accept the proposed action!
If `DragMove` does not accept the proposed action, how can the user drop on `..`?!
Wait... `QListView` natively accepts drops because `dragDropMode` is `DragDrop`.
If `DragMove` falls through to `KXmlGuiWindow::eventFilter(obj, event)`, it passes it to the `QListView`, which MIGHT accept it!
To PREVENT `QListView` from accepting invalid drops, we should `dragEvent->ignore(); return true;` if it's invalid!
Let's see:
```cpp
        } else if (obj == vfsExplorer) {
            QModelIndex index = vfsExplorer->indexAt(dragEvent->position().toPoint());
            if (index.isValid()) {
                QStandardItem* targetItem = vfsModel->itemFromIndex(index);
                QString targetType = targetItem->data(Qt::UserRole + 1).toString();
                if (targetType.endsWith("_folder") || targetItem->text() == "..") {
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
        }
```
If we allow dropping on `..`, what does `moveItemToFolder` do?
If they drop a Document onto `..` inside `docs_folder` (which points to `Book`), it should probably just do NOTHING, but maybe we should explicitly reject the drop in `QEvent::Drop`!
```cpp
                    if (draggedItem && moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true; // Always consume it to prevent native QListView drop!
```
YES! If we ALWAYS `return true` after handling `obj == vfsExplorer && dropEvent->source() == vfsExplorer`, then `QListView` NEVER natively moves the item! It only moves if OUR database code succeeds, which then triggers `loadDocumentsAndNotes()` (which triggers `refreshVfsExplorer()`)!
Actually, wait! `moveItemToFolder` was modified earlier to NOT call `loadDocumentsAndNotes()`? No, I only modified `saveDocBtn`. `moveItemToFolder` STILL calls `loadDocumentsAndNotes()`!
Let's verify!
