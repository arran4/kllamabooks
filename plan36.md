It IS calling `loadDocumentsAndNotes()`.
So the tree is visually rebuilding!
If it's rebuilding, then the DB HAS the update!
If the DB has the update, WHY doesn't it persist when restarting the application?!
Is it possible the database filepath is in `/tmp/`?
No, `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` is `~/.local/share/kllamabooks`!
Wait... "When I do it from the tree it updates in the tree and for the file explorer. However neither changes persist when I restart the application"
Wait... does `openBooksTree` SAVE the order of folders?
"folders" table has `position`.
When moving items, `db->moveItem` DOES NOT update `position`.
Wait! For `documents`, `templates`, `notes`, `drafts`, `messages`... they DO NOT HAVE a `position` column!
They are ordered by `timestamp` or `id`!
So moving a document updates its `folder_id`.
If it doesn't persist... maybe when the app restarts, it's not opening the same DB file?
No, it opens the exact same DB file!
Wait! "neither changes persist when I restart the application, these changes need a consistent internal state that is updated once and broadcast out and updated in teh database too."
If the DB is updated, and it persists on disk, why doesn't it load?
Could `moveItemToFolder` be silently failing?
Wait! In `QTreeView` natively, if you drag and drop, the model moves the item automatically WITHOUT our event filter!
Why? Because `eventFilter` only catches events sent to `openBooksTree->viewport()` if installed on viewport, or `openBooksTree`.
Wait, in `setupUi`:
```cpp
    openBooksTree->installEventFilter(this);
```
Wait! `QTreeView::dropEvent` is received by `openBooksTree->viewport()`, NOT `openBooksTree`!
AHA!
If `eventFilter` is installed on `openBooksTree`, it DOES NOT CATCH `QEvent::Drop` on the items themselves! `QAbstractScrollArea` redirects mouse/drop events to its `viewport()`!
So `openBooksTree->viewport()->installEventFilter(this)` is needed!
Because it's NOT installed on the viewport, `QTreeView` handles the drop natively, moves the item visually, and our `moveItemToFolder` is NEVER called!
YES! THIS IS THE ROOT CAUSE!
The native Qt drag-and-drop moves the `QStandardItem` to the new parent in memory. So the tree updates visually! And since the tree updates visually, the VFS explorer ALSO updates (because of `dataChanged`/`rowsInserted` signals)!
BUT our database code `db->moveItem` is NEVER EXECUTED because the event is bypassed!
When they restart the app, the DB was never updated, so the tree reverts!
WOW!

To fix this:
```cpp
    openBooksTree->viewport()->installEventFilter(this);
    vfsExplorer->viewport()->installEventFilter(this);
```
AND we should also check if we still need `installEventFilter(this)` on the main widgets, maybe yes for `QEvent::DragEnter`.
Wait, `DragEnter`, `DragMove`, `Drop` are sent to the widget OR the viewport?
In `QAbstractScrollArea`, drag and drop events are sent to the `viewport()`.
Let's check `src/MainWindow.cpp` to see how `installEventFilter` is called.
