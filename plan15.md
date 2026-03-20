Let's see:
1. file moves and copies should persist to database
- wait, I saw drag-n-drop bug `sourceBook != targetBook` for vfsExplorer drags.
Let's fix it!
In `eventFilter`, if `dropEvent->source() == vfsExplorer`, then `sourceView->currentIndex()` is an index in `vfsModel`.
`vfsModel` items do not have parents! So `draggedItem->parent()` is null!
Then `sourceBook = draggedItem; while (sourceBook && sourceBook->parent()) sourceBook = sourceBook->parent();` means `sourceBook` is just the `draggedItem`. Which is NOT a book!
So `sourceBook != targetBook` is ALWAYS TRUE. So it fails!
To fix it, we should pass the real tree item!
```cpp
                if (sourceIndex.isValid()) {
                    QStandardItem* draggedItem = nullptr;
                    if (dropEvent->source() == openBooksTree) {
                        draggedItem = openBooksModel->itemFromIndex(sourceIndex);
                    } else {
                        // It's from vfsExplorer!
                        // The item in vfsModel corresponds to an item in openBooksTree.
                        // But wait! vfsModel items have the same UserRole id and UserRole+1 type!
                        // We can just find it in openBooksModel!
                        QStandardItem* vfsItem = vfsModel->itemFromIndex(sourceIndex);
                        if (vfsItem) {
                            if (vfsItem->text() == "..") return true; // Can't drag ".."
                            int id = vfsItem->data(Qt::UserRole).toInt();
                            QString type = vfsItem->data(Qt::UserRole + 1).toString();
                            // Find it in the current book
                            QModelIndex treeIndex = openBooksTree->currentIndex();
                            if (treeIndex.isValid()) {
                                QStandardItem* currentBookOrFolder = openBooksModel->itemFromIndex(treeIndex);
                                QStandardItem* book = currentBookOrFolder;
                                while (book && book->parent()) book = book->parent();
                                draggedItem = findItemRecursive(book, id, type);
                            }
                        }
                    }

                    if (draggedItem && moveItemToFolder(draggedItem, targetItem)) {
                        dropEvent->acceptProposedAction();
                        return true;
                    }
                }
```
Wait! `findItemRecursive` is already available!
```cpp
QStandardItem* MainWindow::findItemRecursive(QStandardItem* parent, int id, const QString& type) {
    if (parent->data(Qt::UserRole).toInt() == id && parent->data(Qt::UserRole + 1).toString() == type) {
        return parent;
    }
    for (int i = 0; i < parent->rowCount(); ++i) {
        QStandardItem* found = findItemRecursive(parent->child(i), id, type);
        if (found) return found;
    }
    return nullptr;
}
```
This is perfect!
