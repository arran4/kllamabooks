Let's figure out all these bugs one by one.

**Issue 4:** There is some state confusion as to how open and closed files works, which allows for duplicate entries.
In `MainWindow::onBookSelected`, if `m_openDatabases.contains(fileName)`:
Wait! If it contains the filename, it opens the database, but it STILL adds it to `openBooksTree`!
Let's look at `onBookSelected`:
```cpp
    if (m_openDatabases.contains(fileName)) {
        currentDb = m_openDatabases[fileName];
    } else {
        // ... loads database
    }
    // Then it adds to tree!
    QStandardItem* bookItem = new QStandardItem(QIcon::fromTheme("application-x-sqlite3"), fileName);
    // ...
    openBooksModel->appendRow(bookItem);
```
Ah! So if you open an already opened book, it adds another duplicate node to the tree!
Also, it removes it from `bookList` without checking if it's there. But if it's open, it shouldn't be in `bookList`? Wait, maybe it can be added to `bookList` again?
If a file is in `m_openDatabases`, we shouldn't add it to `openBooksTree` again. Instead, we should just select the existing node.

**Issue 6:** Opening a chat using the tree view should show us the contents of the chat
In `MainWindow::setupUi`:
```cpp
    connect(openBooksTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        ...
        } else if (item->data(Qt::UserRole).toInt() > 0 && type != "doc_folder" && type != "templates_folder" &&
                   type != "drafts_folder") {
            // It's a chat node. Single clicking displays the chat.
            currentLastNodeId = item->data(Qt::UserRole).toInt();
            if (currentDb) {
                updateLinearChatView(currentLastNodeId, currentDb->getMessages());
                mainContentStack->setCurrentWidget(chatWindowView);
            }
        }
```
Wait, `openBooksTree->selectionModel()->selectionChanged` also triggers `onOpenBooksSelectionChanged`, which does:
```cpp
    } else {
        int nodeId = item->data(Qt::UserRole).toInt();
        if (type == "document" || type == "template" || type == "draft") {
            // ...
        } else if (type == "note") {
            // ...
        } else {
            // It's a chat leaf or fork point
            currentLastNodeId = nodeId;
            isCreatingNewChat = (nodeId == 0);
            if (isCreatingNewChat && item->parent()) {
                currentChatFolderId = item->parent()->data(Qt::UserRole).toInt();
            }
            if (currentDb) {
                updateLinearChatView(currentLastNodeId, currentDb->getMessages());
                mainContentStack->setCurrentWidget(chatWindowView);
            }
        }
    }
```
If we select a chat in the tree view, it sets `currentLastNodeId` and shows `chatWindowView`. But `openBooksTree` `doubleClicked` vs `onOpenBooksSelectionChanged`.
Wait, "Currently opening a chat isn't showing the contents, we expect to see the chat contents when we open it"
Could it be that "Opening a chat" means expanding the chat fork folder?
No, it says "Currently opening a chat isn't showing the contents". When we double click or single click?
If I open a chat session (root chat), its type is `chat_session`. Wait! In `populateChatFolders`, root messages get type `chat_node`. Let's look at `populateChatFolders`.
