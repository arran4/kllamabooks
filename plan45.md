Let's see the remaining problems.
1. "When I move something in the file view the tree view completely refreshes which causes it to forget which nodes were open / collapsed, which is disorientating to the users it should be an update operation not a complete refresh of the data"
Ah! When I move something, `moveItemToFolder` returns `true`, but it STILL calls `loadDocumentsAndNotes()` inside!
Wait, in my `fix_ui_updates.patch` (plan25), I only removed `loadDocumentsAndNotes()` from `saveDocBtn` and `saveNoteBtn`! I DID NOT remove it from `moveItemToFolder`!
If `moveItemToFolder` succeeds, it calls `loadDocumentsAndNotes()`, which rebuilds the tree!
To fix this, instead of calling `loadDocumentsAndNotes()`, we should just manually update the tree!
Wait! We already HAVE the tree pointers! `draggedItem` and `targetItem`!
If we move an item from `draggedItem` to `targetItem`, we can just do:
```cpp
    QStandardItem* parent = draggedItem->parent();
    if (parent) {
        QStandardItem* takenItem = parent->takeRow(draggedItem->row()).first();
        targetItem->appendRow(takenItem);
    }
```
If we do this, the tree is manually updated IN PLACE, and we DON'T need to call `loadDocumentsAndNotes()`!

2. "Moves on the tree view no longer work via drag and drop can we please fix that"
Wait! Why do moves on the tree view no longer work?!
Because of my last patch!
```cpp
        } else if ((obj == openBooksTree || obj == openBooksTree->viewport() || obj == vfsExplorer || obj == vfsExplorer->viewport()) &&
                   (sourceView == openBooksTree || sourceView == vfsExplorer)) {
            // ...
            if (targetItem) {
                // ...
            }
            dropEvent->ignore();
            return true;
        }
```
Wait! In the tree view, `QDropEvent` is handled by `openBooksTree->viewport()`.
`sourceView` is `openBooksTree`.
`targetView` is `openBooksTree`.
`targetIndex` is evaluated using `dropEvent->position().toPoint()`.
If I drag a file onto a folder in the tree view, `targetIndex` points to the folder. `targetItem` is the folder.
`moveItemToFolder` is called. It updates the DB.
If it returns `true`, `dropEvent->acceptProposedAction()` is called.
BUT wait! My `dropEvent->ignore()` at the end is reached IF `moveItemToFolder` returns `false` or if `targetIndex` is invalid.
If `targetIndex` is valid, and `moveItemToFolder` is called, it returns `true`!
If it returns `true`, why would it not work?
Wait, "Moves on the tree view no longer work via drag and drop".
Let's see what happens if I drag a file into a folder in the tree view.
`draggedItem = openBooksModel->itemFromIndex(sourceIndex);`
BUT wait! `sourceIndex = sourceView->currentIndex();`!
If I click and drag a file in the tree, DOES `currentIndex()` update?!
Earlier I hypothesized that `currentIndex()` updates on click.
But what if the user clicks and drags a file that is ALREADY selected? It works.
What if they click and drag a DIFFERENT file?
Maybe `currentIndex()` is NOT the file they are dragging!
Or maybe `moveItemToFolder` is returning `false`?!
Wait! I added `openBooksTree->viewport()->installEventFilter(this);`.
This means ALL drops to the tree are intercepted!
If they are intercepted, the native Qt drop is blocked.
Wait! NATIVE Qt drop not only visually moves it, but it handles the actual visual feedback of "dropping between rows" vs "dropping ON rows".
If `targetIndex` is valid, `targetItem` is the folder.
But what if `dropEvent->dropAction()` is `Qt::MoveAction`?
If `moveItemToFolder` returns `true`, and I do `dropEvent->acceptProposedAction()`, `return true;`.
If I `return true`, `QTreeView` NEVER receives the event!
If `QTreeView` never receives the event, it NEVER MOVES THE ITEM VISUALLY!
Ah!!!!!!
In VFS, when I move an item, `loadDocumentsAndNotes()` rebuilds the tree, so it visually moves!
But in the Tree, if I return `true`, `QTreeView` is blocked from its native logic!
Wait, but if `loadDocumentsAndNotes()` rebuilds the tree, then the Tree SHOULD visually update anyway?!
Wait, does `loadDocumentsAndNotes()` rebuild the tree?!
Yes! So why doesn't it visually update in the tree?!
"Moves on the tree view no longer work via drag and drop can we please fix that"
If it doesn't work, maybe `moveItemToFolder` is returning `false`?
Why would `moveItemToFolder` return `false` for the tree?
Let's trace `QEvent::Drop` for the tree.
`targetIndex` = `indexAt(pos)`.
Is `targetItem` found? Yes.
Is `draggedItem` found? Yes.
`moveItemToFolder` is called.
`sourceBook == targetBook` is checked.
If I drag inside the same book, they are the same!
Why would it fail?
Wait! In `moveItemToFolder`:
```cpp
    if (draggedItem == targetItem) return false;

    if (!targetType.endsWith("_folder")) {
        if (targetItem->parent()) {
            targetItem = targetItem->parent();
            ...
```
If I drag a file into a folder, it works!
Why would "Moves on the tree view no longer work"?!
Ah! What if the user drags a folder into a folder?
`itemType.endsWith("_folder") && targetType == itemType`
Wait, what if `targetType` is not equal to `itemType`?
"1 folder 'Folder' and 1 file 'File' in Documents"
If they drag 'File' into 'Folder'. `itemType` = "document". `targetType` = "docs_folder".
`compatible = true`.
So it returns `true`!
Why would it NOT work?
Wait, if it returns `true`, `loadDocumentsAndNotes()` runs. The tree is rebuilt!
But wait! If the tree is rebuilt inside the `Drop` event handler...
Rebuilding the tree deletes all items and recreates them!
If you delete the items while `QTreeView` is processing the drag-and-drop...
Wait, we returned `true` from `eventFilter`! So `QTreeView` does NOT process the drop event!
But `QDrag` is still active!
If `QDrag` is active, and we delete the model items, maybe it crashes or fails?
The user said "Moves on the tree view no longer work". Meaning nothing happens? Or it doesn't persist?
"Moves on the tree view no longer work via drag and drop can we please fix that"

What if `sourceIndex` was NOT the dragged item?!
How does one get the dragged item from `QDropEvent`?
`dropEvent->mimeData()`!
Qt's `QStandardItemModel` serializes the dragged items into `mimeData`!
BUT we are NOT decoding `mimeData`! We are using `sourceView->currentIndex()`!
If `currentIndex()` is NOT the dragged item, then `draggedItem` is wrong!
Wait! In `vfsExplorer`, we also used `sourceView->currentIndex()`. And the user said: "changes made using the explorer are now persisting".
So `currentIndex()` WORKS for VFS Explorer!
Why wouldn't it work for the Tree?!
Maybe because `QTreeView` supports multiple selection?
Wait! If `QTreeView` drop doesn't work, let's fix it by manually updating the tree instead of `loadDocumentsAndNotes()`!
If we manually update the tree:
```cpp
        if (!isCopy) {
            QStandardItem* parent = draggedItem->parent();
            if (parent) {
                QList<QStandardItem*> taken = parent->takeRow(draggedItem->row());
                targetItem->appendRow(taken);
            }
        }
```
If we do this, the NATIVE drop is blocked, but we MANUALLY move the item!
And the database is updated!
And the tree DOES NOT rebuild, so it retains its expanded state!
This fixes #1 AND #2 at the same time!

Let's look at `BookDatabase::moveItem` again.
If I drag from tree to tree, `targetIndex` is the item under the mouse.
If `targetIndex` is invalid, the user dropped between items?
If we block it, it does nothing.

Let's implement the manual tree update in `moveItemToFolder` instead of `loadDocumentsAndNotes()`.

3. "The breadcrumbs don't take me up to parent nodes when I click on the parent nodes"
Wait, breadcrumbs are built in `updateBreadcrumbs()`.
```cpp
        } else {
            QLineEdit* edit = new QLineEdit(parts[i], this);
            // ...
```
If they click on a parent node in the breadcrumbs, it doesn't take them there!
Wait! The breadcrumbs have a function `onBreadcrumbClicked(const QString& type, int id)` which is currently empty!
```cpp
void MainWindow::onBreadcrumbClicked(const QString& type, int id) {
    // Allows jumping up the tree
}
```
And in `updateBreadcrumbs`:
```cpp
    for (int i = 0; i < parts.size(); ++i) {
        if (i < parts.size() - 1) {
            QLabel* label = new QLabel(parts[i], this);
            breadcrumbLayout->addWidget(label);
            breadcrumbLayout->addWidget(new QLabel(">", this));
        } else {
            // editable line edit
```
Wait, the `QLabel`s are NOT clickable!
To make them clickable, we could use `QPushButton` with flat style!
```cpp
    for (int i = 0; i < parts.size(); ++i) {
        if (i < parts.size() - 1) {
            QPushButton* btn = new QPushButton(parts[i], this);
            btn->setFlat(true);
            btn->setCursor(Qt::PointingHandCursor);
            // We need to know the id and type of this parent!
```
But wait, how do we know the ID and type of the parent node in `updateBreadcrumbs()`?
`parts` is just a `QStringList`!
We should trace from `treeItem` UP to the root!
```cpp
    QList<QStandardItem*> pathItems;
    QStandardItem* curr = treeItem;
    while (curr) {
        pathItems.prepend(curr);
        curr = curr->parent();
    }
```
Then for each item in `pathItems`:
```cpp
    for (int i = 0; i < pathItems.size(); ++i) {
        QStandardItem* nodeItem = pathItems[i];
        if (i < pathItems.size() - 1) {
            QPushButton* btn = new QPushButton(nodeItem->text(), this);
            btn->setFlat(true);
            btn->setCursor(Qt::PointingHandCursor);
            connect(btn, &QPushButton::clicked, this, [this, nodeItem]() {
                openBooksTree->setCurrentIndex(nodeItem->index());
            });
            breadcrumbLayout->addWidget(btn);
            breadcrumbLayout->addWidget(new QLabel(">", this));
        } else {
            // The editable line edit for the current item
```
This is brilliant and simple! We don't even need `onBreadcrumbClicked`!
