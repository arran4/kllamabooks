Let's see what is meant by "the central widget file explorer still doesn't maintain sync iwth the vfs unlike the tree view".
`vfsExplorer` is a `QListView` driven by `vfsModel`.
When does `vfsModel` update?
It updates ONLY in `onOpenBooksSelectionChanged`!
```cpp
    if (type == "book" || type == "chats_folder" || type == "docs_folder" ||
        type == "notes_folder" || type == "templates_folder" || type == "drafts_folder" || (type == "chat_node" && item->rowCount() > 0)) {

        vfsModel->clear();
        // Add ".."
        for (int i = 0; i < item->rowCount(); ++i) {
            // Adds children to vfsModel
        }
        mainContentStack->setCurrentWidget(vfsExplorer);
    }
```
If we create a new item inside a folder while viewing it in `vfsExplorer` (e.g. by right clicking in the tree and selecting "New Folder" or "New Document"):
The `item` is modified in the tree! `item->appendRow(newItem)`.
BUT `vfsModel` is NOT updated!
Because adding a row to `openBooksModel` does NOT trigger `selectionChanged` on `openBooksTree`!
So the newly created item appears in the tree, but `vfsExplorer` doesn't see it until you click away and click back!
Similarly, if we rename an item in the tree (via Breadcrumbs or otherwise), `vfsModel` doesn't update its text!
Similarly, if we drag and drop a file out of the folder, `vfsModel` doesn't update!
"The central widget file explorer still doesn't maintain sync iwth the vfs unlike the tree view."
To fix this, we should connect to the `openBooksModel`'s signals: `rowsInserted`, `rowsRemoved`, `dataChanged`, `modelReset`.
When any of those happen, IF the current `mainContentStack` is `vfsExplorer`, we should rebuild `vfsModel` based on `openBooksTree->currentIndex()`!
Actually, even easier:
Just listen to `openBooksModel->itemChanged`? No, `dataChanged` covers that.
What about `rowsInserted` and `rowsRemoved`?
```cpp
    connect(openBooksModel, &QStandardItemModel::dataChanged, this, [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
        // Just refresh the VFS if the changed item is a child of the current folder
        if (mainContentStack->currentWidget() == vfsExplorer) {
            QModelIndex currentIdx = openBooksTree->currentIndex();
            if (topLeft.parent() == currentIdx) {
                // One of the children changed! Refresh!
                emit openBooksTree->selectionModel()->selectionChanged(QItemSelection(currentIdx, currentIdx), QItemSelection());
            }
        }
    });
    connect(openBooksModel, &QStandardItemModel::rowsInserted, this, [this](const QModelIndex& parent, int first, int last) {
        if (mainContentStack->currentWidget() == vfsExplorer) {
            QModelIndex currentIdx = openBooksTree->currentIndex();
            if (parent == currentIdx) {
                emit openBooksTree->selectionModel()->selectionChanged(QItemSelection(currentIdx, currentIdx), QItemSelection());
            }
        }
    });
    connect(openBooksModel, &QStandardItemModel::rowsRemoved, this, [this](const QModelIndex& parent, int first, int last) {
        if (mainContentStack->currentWidget() == vfsExplorer) {
            QModelIndex currentIdx = openBooksTree->currentIndex();
            if (parent == currentIdx) {
                emit openBooksTree->selectionModel()->selectionChanged(QItemSelection(currentIdx, currentIdx), QItemSelection());
            }
        }
    });
```
Wait! `openBooksTree->selectionModel()->selectionChanged` is a signal, we shouldn't `emit` it from outside to force an update.
We can extract the logic inside `onOpenBooksSelectionChanged` that populates the `vfsModel` into a new function: `void MainWindow::refreshVfsExplorer()`!
And call `refreshVfsExplorer()` when:
- `onOpenBooksSelectionChanged`
- `rowsInserted`, `rowsRemoved`, `dataChanged` on `openBooksModel` if the parent matches `openBooksTree->currentIndex()`.

Let's check `MainWindow.h` to add `void refreshVfsExplorer();`.
