"1. drag and drop onto valid targets such as a folder inside the file explorer is no longer working as it says it's an invalid target but a file onto a folder should be, this has previously worked"

Wait! In `vfsExplorer`, my previous fix for `DragMove` was:
```cpp
        } else if (obj == vfsExplorer || obj == vfsExplorer->viewport()) {
            dragEvent->acceptProposedAction();
            return true;
        }
```
If it's always accepting, why does it say it's an invalid target?
Wait! `QListView` in `IconMode` with `setMovement(QListView::Static)`.
If `setMovement` is `Static`, it might block dropping entirely if it thinks items cannot be moved around!
Wait, `Static` means items cannot be moved by the user in the list view (i.e. you can't reorder them). Does it also disable dropping?
YES! In Qt, if a view is `Static`, it disables internal drag and drop!
Let's remove `vfsExplorer->setMovement(QListView::Static);`!

"2. file exporer to tree view drag and drop is still not working, it is correctly registring as a valid drop target but it isn't moving or copying when I drop"
"3. Tree to tree view drag drops are not working but it is registing as a valid target"

Both #2 and #3 are the SAME bug! "it isn't moving or copying when I drop".
Why?
If I removed `QTimer::singleShot` and changed it to:
```cpp
    if (compatible && db->moveItem(table, itemId, targetFolderId)) {
        QStandardItem* parent = draggedItem->parent();
        if (parent) {
            QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
            targetItem->appendRow(taken);
            openBooksTree->setExpanded(targetItem->index(), true);
        }
        return true;
    }
```
If it returns `true`, then in `QEvent::Drop`:
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If it returns `true`, it calls `dropEvent->acceptProposedAction();` and returns `true`.
Why does it not do anything?!
Because `parent->takeRow` and `targetItem->appendRow` run, but they FAIL?!
Wait! `takeRow` DOES NOT fail if the item is in the model.
Wait... what if `draggedItem` is a pointer to an item that is ALREADY DELETED?
NO.
Wait... what if `targetItem->appendRow(taken)` modifies the model, but `QTreeView` doesn't visually update because the drag operation aborted it?
Let's just use `loadDocumentsAndNotes()` AGAIN, but this time preserve the expanded state!!!
If I use `loadDocumentsAndNotes()`, it works 100% of the time, because it rebuilds from the DB, and it bypasses any weird `QStandardItemModel` drag-and-drop state machine issues!
And I can just write a small helper to save and restore the expanded nodes!

Let's look at `loadDocumentsAndNotes()`:
If we save expanded nodes:
```cpp
void MainWindow::saveExpandedState(QTreeView* tree, QStandardItem* item, QSet<int>& expanded) {
    if (!item) return;
    if (tree->isExpanded(item->index())) {
        expanded.insert(item->data(Qt::UserRole).toInt()); // Use ID to track it
    }
    for (int i = 0; i < item->rowCount(); ++i) {
        saveExpandedState(tree, item->child(i), expanded);
    }
}

void MainWindow::restoreExpandedState(QTreeView* tree, QStandardItem* item, const QSet<int>& expanded) {
    if (!item) return;
    if (expanded.contains(item->data(Qt::UserRole).toInt())) {
        tree->setExpanded(item->index(), true);
    }
    for (int i = 0; i < item->rowCount(); ++i) {
        restoreExpandedState(tree, item->child(i), expanded);
    }
}
```
Wait, for root items (books), their ID is 0!
So `expanded.insert(0)` would expand all books. But we might want to track by text + ID?
Or we can just use `QString` paths!
```cpp
QString getPath(QStandardItem* item) {
    QString path = item->text();
    QStandardItem* p = item->parent();
    while (p) {
        path = p->text() + "/" + path;
        p = p->parent();
    }
    return path;
}
```
This is perfect and unique!

```cpp
void saveExpandedState(QTreeView* tree, QStandardItem* item, QSet<QString>& expanded) {
    if (!item) return;
    if (tree->isExpanded(item->index())) {
        QString path = item->text();
        QStandardItem* p = item->parent();
        while (p) { path = p->text() + "/" + path; p = p->parent(); }
        expanded.insert(path);
    }
    for (int i = 0; i < item->rowCount(); ++i) saveExpandedState(tree, item->child(i), expanded);
}
void restoreExpandedState(QTreeView* tree, QStandardItem* item, const QSet<QString>& expanded) {
    if (!item) return;
    QString path = item->text();
    QStandardItem* p = item->parent();
    while (p) { path = p->text() + "/" + path; p = p->parent(); }
    if (expanded.contains(path)) tree->setExpanded(item->index(), true);
    for (int i = 0; i < item->rowCount(); ++i) restoreExpandedState(tree, item->child(i), expanded);
}
```
If we wrap `loadDocumentsAndNotes()` with this:
```cpp
void MainWindow::loadDocumentsAndNotes() {
    QSet<QString> expanded;
    if (openBooksModel && openBooksModel->invisibleRootItem()) {
        for (int i = 0; i < openBooksModel->rowCount(); ++i) {
            saveExpandedState(openBooksTree, openBooksModel->item(i), expanded);
        }
    }
    // ... rest of loadDocumentsAndNotes ...
    if (openBooksModel && openBooksModel->invisibleRootItem()) {
        for (int i = 0; i < openBooksModel->rowCount(); ++i) {
            restoreExpandedState(openBooksTree, openBooksModel->item(i), expanded);
        }
    }
}
```
Wait, `loadDocumentsAndNotes()` does NOT have `saveExpandedState` as a method. We can just put it inside `MainWindow.cpp` as an anonymous namespace helper!
YES!
If we do this, we can revert `moveItemToFolder` to JUST CALL `loadDocumentsAndNotes()`, which we KNOW works flawlessly!

Why did the user say "it doesn't do anything"?
Because `targetItem->appendRow(taken)` DOES NOT WORK inside `QEvent::Drop` when the item is part of the ongoing drag operation! The view's drag-and-drop state machine aborts the visual update if the model is structurally mutated via `takeRow` underneath it before it finishes processing the drop!
BUT `loadDocumentsAndNotes` completely removes and repopulates the folders, bypassing the exact item instance tracking!
Actually, `loadDocumentsAndNotes` DOES `docsFolder->removeRows(0, docsFolder->rowCount())`.
If we rebuild the whole folder, it works!

Let's test this approach!
