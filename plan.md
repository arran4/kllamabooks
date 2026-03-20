1. file moves and copies should persist to database
- `MainWindow::moveItemToFolder` is implemented but seems incomplete. Need to check if it's called and working correctly. Ensure it updates `BookDatabase::moveItem` or `BookDatabase::moveFolder`. Wait, in `MainWindow::moveItemToFolder` it seems to only do `moveFolder` or `moveItem`. Does it handle copies?
2. The central list widget for the file explorer drag and drop file operations don't work the same as the file operations on the list view. these should be synced
- The `vfsExplorer` is the central list widget. Its drag and drop operations don't work the same as the tree view (`openBooksTree`).
3. chat forks are broken. `Path:` doesn't make sense so should be remove. `Fork:` Doesn't need to be directly called out.
- Update `populateChatFolders` and `populateMessageForks` in `MainWindow.cpp` to remove "Path:" and "Fork:". Also they should use the fork icon for forks.
4. There is some state confusion as to how open and closed files works, which allows for duplicate entries.
- Probably in `MainWindow::onBookSelected` or related functions where files are added to the list or tree. Need to check uniqueness.
5. Double clicking not single clicking should open a database
- Change `QListWidget::clicked` connection to `QListWidget::doubleClicked` for `bookList` in `MainWindow::setupUi`.
6. Opening a chat using the tree view should show us the contents of the chat
- `openBooksTree` double click/single click logic.
7. A fork always uses the fork icon
- In `populateChatFolders`/`populateMessageForks`.
8. Currently opening a chat isn't showing the contents, we expect to see the chat contents when we open it
- Double click or single click in tree view should update linear chat view and switch stack.
9. Forks are virtual folders and should be openable, if they aren't then they aren't a fork.
- Forks should be expandable in the tree view and the list view.
10. We need to save the state of "new items" and modified+unsaved items as drafts, until they are saved. (We can still create drafts directly) Once saved drafts no longer save.
- When creating a new document, save it as a draft first. When saving, remove it from drafts and put it in its actual type.
11. The breadcrumbs stop working and get stuck on an item, they should update as I look at things
- `updateBreadcrumbs` might have a bug where it's not being called or correctly tracking the item.
12. Items in the tree view and list view that have notifications should display differently
- They use `NotificationDelegate` which relies on role `Qt::UserRole + 10`. Wait, what does it do? Check `NotificationDelegate.cpp`.
Double clicking to open db solved.

Now for:
2. central list widget for file explorer drag and drop file operations don't work the same as the file operations on the list view. these should be synced
- Wait, where are the drag/drop events defined for vfsExplorer vs openBooksTree?
- `MainWindow::eventFilter` handles DragEnter, DragMove, Drop.
- Wait, the issue says: "The central list widget for the file explorer drag and drop file operations don't work the same as the file operations on the list view. these should be synced".
- openBooksTree vs vfsExplorer drag and drop.
Double clicking to open db solved.

Now for:
2. central list widget for file explorer drag and drop file operations don't work the same as the file operations on the list view. these should be synced
- In `QEvent::DragMove`, it only handles `obj == openBooksTree` for checking if target is a folder to accept the action. It should probably also handle `obj == vfsExplorer`.
- I should modify `DragMove` to also accept drops for `vfsExplorer` if the target is a folder, or the background.
Wait, `vfsExplorer` also has `DragMove` event.

Let's check `moveItemToFolder`.
Wait, what is missing for "file moves and copies should persist to database (basically we should be able to store the delta or rewrite the structure back to the database.)"?
- `moveItemToFolder` seems to persist it using `db->moveFolder(itemId, targetFolderId)` and `db->moveItem(table, itemId, targetFolderId)`.
- But what if it's a drag between different books? `sourceBook != targetBook` returns false. Wait, can you copy across databases? "file moves and copies should persist to database" - maybe it wants to be able to drag a chat session or document from one DB to another?
Wait, if it's a drag between the tree and `vfsExplorer`?
If `draggedItem` is from `vfsModel`, then `sourceBook` will be null, because items in `vfsModel` do not have parents going up to a book item!
Ah!
`vfsModel` items don't have parents. So `sourceBook` will just be the item itself. And then it says `if (!sourceBook || !targetBook || sourceBook != targetBook) return false;`. That's why dragging from `vfsExplorer` fails entirely.
Wait, if I drag from `vfsExplorer` to `openBooksTree` or vice versa, `sourceBook` will be wrong for the `vfsExplorer` item. We need to find the `sourceBook` by using the currently open book that the `vfsExplorer` is displaying.
The `vfsExplorer` displays the children of `openBooksTree->currentIndex()`.
So if an item is dragged from `vfsExplorer`, its "book" is the currently selected book in `openBooksTree`.
