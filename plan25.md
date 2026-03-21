1. **Toolbars on documents**:
We will move the doc buttons and note buttons into `QToolBar`s that we place at the top of the layout instead of bottom.
Let's check `setupUi`:
```cpp
    docContainer = new QWidget(this);
    QVBoxLayout* docLayout = new QVBoxLayout(docContainer);
    docLayout->setContentsMargins(0,0,0,0);

    QToolBar* docToolbar = new QToolBar(this);
    saveDocBtn = new QPushButton(QIcon::fromTheme("document-save"), "Save Document", this);
    QPushButton* backToDocsBtn = new QPushButton(QIcon::fromTheme("go-previous"), "Back to Documents", this);
    docToolbar->addWidget(backToDocsBtn);
    docToolbar->addWidget(saveDocBtn);
    docLayout->addWidget(docToolbar);

    documentEditorView = new QTextEdit(this);
    docLayout->addWidget(documentEditorView);
```
Same for `noteContainer`.

2. **Saving doesn't exit, and avoid tree rebuild**:
Inside `saveDocBtn` lambda:
```cpp
    // INSTEAD OF:
    // loadDocumentsAndNotes(); // Refresh tree to show new title
    // DO:
    if (item) {
        item->setText(currentTitle);
    }
```
Wait! If we create a NEW item (`isCreatingNewDoc`), we need to add the item to the tree without rebuilding!
Wait, if it's a new item, it's already a phantom item! We can just update the phantom item's data!
```cpp
        if (isCreatingNewDoc && item) {
            ...
            item->setData(newId, Qt::UserRole);
            item->setData(type, Qt::UserRole + 1);
            item->setText(currentTitle);
            QFont f = item->font();
            f.setItalic(false);
            item->setFont(f);
            isCreatingNewDoc = false;
        } else if (currentDocumentId != 0) {
            item->setText(currentTitle);
        }
```
And we NEVER call `loadDocumentsAndNotes()` in `saveDocBtn`! This avoids tree rebuild, prevents losing selection, and avoids exiting!
Same for Notes!

3. **Make sure VFS updates and tree refreshes don't lose orientation**:
If we create folders or rename items, we should update the item directly instead of `loadDocumentsAndNotes()`.
```cpp
            currentDb->addFolder(parentId, name, fType);
            // In create folder lambda:
            QStandardItem* fItem = new QStandardItem(QIcon::fromTheme("folder-open"), name);
            fItem->setData(newFolderId, Qt::UserRole);
            fItem->setData(item->data(Qt::UserRole+1), Qt::UserRole+1);
            item->appendRow(fItem);
            openBooksTree->setExpanded(item->index(), true);
```
Wait, we need the new ID! `addFolder` returns the ID.
```cpp
            int newFId = currentDb->addFolder(parentId, name, fType);
            QStandardItem* fItem = new QStandardItem(QIcon::fromTheme("folder-open"), name);
            fItem->setData(newFId, Qt::UserRole);
            fItem->setData(type, Qt::UserRole + 1);
            item->appendRow(fItem);
            openBooksTree->setExpanded(item->index(), true);
```

4. **Right click on fork folder**:
In `showVfsContextMenu`:
```cpp
    if (currentFolderType == "chats_folder" || currentFolderType == "chat_session" || currentFolderType == "chat_node") {
```
In `showOpenBookContextMenu`:
```cpp
    } else if (type == "chats_folder" || type == "docs_folder" || type == "notes_folder" || type == "templates_folder" || type == "drafts_folder" || (type == "chat_node" && item->rowCount() > 0)) {
```
This enables "New Chat" etc on forks.

5. **Reply to fork doesn't create a new tier**:
When I send a message, `onSendMessage` does:
```cpp
    int parentId = isCreatingNewChat ? 0 : currentLastNodeId;
    int userMsgId = currentDb->addMessage(parentId, text, "user", folderId);
    int aiId = currentDb->addMessage(userMsgId, "...", "assistant");
    currentLastNodeId = aiId;
```
Then it calls:
```cpp
    // 4. Refresh tree if needed (especially for new chats)
    if (chatsFolder) {
        chatsFolder->removeRows(0, chatsFolder->rowCount());
        populateChatFolders(chatsFolder, 0, currentDb->getMessages(), currentDb.get());
        openBooksTree->expandAll();
    }
```
This complete rebuild of the tree causes loss of orientation!
We should just append to the existing tree!
Wait, if it's an existing chat, we DO NOT need to add anything to the tree! Why? Because the tree only shows FORKS and LEAVES!
If a leaf just gets a new message, the leaf is STILL the leaf! The ID of the leaf changes to `aiId`!
So we just find the current leaf item, and update its `Qt::UserRole` to `aiId`!
```cpp
    if (isCreatingNewChat) {
        // Change the phantom item to a real chat session item!
        QModelIndex idx = openBooksTree->currentIndex();
        QStandardItem* item = openBooksModel->itemFromIndex(idx);
        if (item && item->data(Qt::UserRole+1) == "chat_session") {
            item->setData(aiId, Qt::UserRole);
            item->setData("chat_node", Qt::UserRole+1);
            item->setText(text.left(30));
            QFont f = item->font(); f.setItalic(false); item->setFont(f);
        }
    } else {
        QStandardItem* item = findItemInTree(parentId, "chat_node");
        if (item) {
            if (item->rowCount() > 0) {
                // It was a fork! Sending a message creates a new branch!
                QStandardItem* branchItem = new QStandardItem(QIcon::fromTheme("text-x-generic"), text.left(30));
                branchItem->setData(aiId, Qt::UserRole);
                branchItem->setData("chat_node", Qt::UserRole+1);
                item->appendRow(branchItem);
                // Also update the icon of the parent to be a fork if it wasn't already (though it already has rowCount > 0, so it was)
                // Actually, if we just replied to an existing message in the middle of a path, that message becomes a fork point!
                // But wait! If we reply to the LEAF, it's NOT a fork!
            } else {
                // We replied to the leaf! Just update the leaf's ID!
                item->setData(aiId, Qt::UserRole);
                item->setText(text.left(30));
            }
        }
    }
```
Wait, wait. If we reply to a message in the middle of a linear path (which is not in the tree!), how do we find it?
Ah! If we edit history, `discardChangesBtn` shows up, and if we type, we create a fork!
But if we just append to the end of the chat, we are at `currentLastNodeId`. `currentLastNodeId` IS the leaf node, which IS in the tree!
So `findItemInTree(currentLastNodeId, "chat_node")` will find it!
And since it's a leaf, `rowCount == 0`. We just update its ID to `aiId`!
Wait, but if we reply to a fork point (which is in the tree, and has `rowCount > 0`), we append a new row to it!
This perfectly handles the tier issue ("When you reply to a fork folder, it creates the new leaf at the same level of the fork and doesn't create a new tier."). Yes! A child row means it is INSIDE the fork folder in the tree!

Let's test this logic!
