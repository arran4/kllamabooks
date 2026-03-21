`getDocuments(folderId)` selects `WHERE folder_id = ?` if `folderId != -1`.
So if `folder_id` in the DB was updated, it SHOULD be fetched under that new folder!
Then why does it NOT persist?
Is `db->moveItem` rolling back?!
Wait... SQLCipher might need a transaction to commit if we explicitly used `BEGIN` somewhere? We didn't.
But wait! "When I do it from the tree it updates in the tree and for the file explorer."
If the tree updates, `moveItemToFolder` returns `true`.
If it returns `true`, `db->moveItem` returns `true`.
If `db->moveItem` returns `true`, the `UPDATE` query succeeded!
So the DB HAS the new `folder_id`!
But "neither changes persist when I restart the application".
When does the application save?
SQLite writes immediately!
Unless... wait. Are we using an in-memory database?
No, `BookDatabase::open` opens `m_filepath`.
Is `m_filepath` being moved or replaced?
No.

Wait!!! "I drag and drop it using the list view file explorer which si the central widget, it doesn't persist, or update the tree. When I do it from the tree it updates in the tree and for the file explorer. However neither changes persist when I restart the application, these changes need a consistent internal state that is updated once and broadcast out and updated in teh database too."
What if the NATIVE `QTreeView` / `QListView` is doing the visual move, but `moveItemToFolder` is returning `false` BOTH times?
If `moveItemToFolder` returns `false`, `dropEvent->ignore()` is called.
BUT does `QTreeView` still move it?!
Wait! I modified `eventFilter` for `openBooksTree` AND `vfsExplorer`:
```cpp
                    if (draggedItem &&
                        moveItemToFolder(draggedItem, targetItem, dropEvent->dropAction() == Qt::CopyAction)) {
                        dropEvent->acceptProposedAction();
                    } else {
                        dropEvent->ignore();
                    }
                    return true;
```
If we `return true`, `QTreeView` does NOT see the `Drop` event! So `QTreeView` CANNOT NATIVELY MOVE IT!
If `QTreeView` cannot natively move it, then the ONLY WAY it moves visually is if `loadDocumentsAndNotes()` runs!
Wait... `openBooksTree` natively moves things via `dropMimeData` in `openBooksModel`?
No, if `QTreeView::dropEvent` is blocked, `QStandardItemModel` doesn't get it.
But wait! What if `moveItemToFolder` returns `true` and calls `loadDocumentsAndNotes()`...
Is it possible `loadDocumentsAndNotes()` is NOT called because `moveItemToFolder` returns `true` but it doesn't call `loadDocumentsAndNotes()`?
In my earlier patch `prevent-tree-rebuilds` (Plan 25):
Wait, did I remove `loadDocumentsAndNotes` from `moveItemToFolder`?
Let's check!
