Issue 6 and 8: "Opening a chat using the tree view should show us the contents of the chat" and "Currently opening a chat isn't showing the contents"
If I look at `QTreeView::doubleClicked` connection in `MainWindow::setupUi`:
```cpp
    connect(openBooksTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        // ...
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
Wait, wait. "Single clicking displays the chat" is in the comments! But the signal is `doubleClicked`!
And `onOpenBooksSelectionChanged` handles single click (selection change):
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
Wait, if it's already doing that on selection change, then why does the user complain "Currently opening a chat isn't showing the contents"?
Maybe because the node ID of a fork point is NOT the end of a chat. It's the point where it branched.
When they open a chat from the tree view, maybe it's not expanding properly?
Wait! In `populateChatFolders`, if it's a root message, it's given the type `chat_node`.
But in `onOpenBooksSelectionChanged`, the type is checked:
```cpp
    QString type = item->data(Qt::UserRole + 1).toString();
    if (type == "book" || type == "chats_folder" || type == "docs_folder" ||
        type == "notes_folder" || type == "templates_folder" || type == "drafts_folder") {

        vfsModel->clear();
        ...
```
If I click on `chat_node`, it falls into `else`, and it calls `updateLinearChatView(currentLastNodeId, currentDb->getMessages())`.
Why wouldn't it show the contents?
Wait! When we load the book, we call `loadSession(0)`.
In `loadSession`, we call `updateLinearChatView(currentLastNodeId, msgs)`.
`updateLinearChatView` has a bug where if we try to open a chat node from the tree view, it doesn't show the contents?
Let's check `updateLinearChatView`:
```cpp
    chatTextArea->blockSignals(true);
    chatTextArea->clear();
    currentChatPath.clear();
```
And then:
```cpp
    if (tailNodeId == 0 || allMessages.isEmpty()) {
        return;
    }

    getPathToRoot(tailNodeId, allMessages, currentChatPath);

    QString userName = currentDb ? currentDb->getSetting("book", 0, "userName", "User") : "User";
    QString assistantName = currentDb ? currentDb->getSetting("book", 0, "assistantName", "Assistant") : "Assistant";

    for (const auto& node : currentChatPath) {
        QString roleName = (node.role == "user") ? userName : assistantName;
        // ...
        chatTextArea->insertPlainText(node.content + "\n\n");
    }
```
If we select a root node, `tailNodeId` is the id of the root node. So it only shows the root message! It does NOT show the full chat down to the leaf!
Because a "chat session" is really the whole path from root to leaf. But if they click the root node in the tree, we only know the root ID!
Ah! A fork node in the tree only has the ID of that fork. It doesn't know which leaf to show!
Wait, but if they click a fork node, they DO just want to see up to the fork point! That's what a fork point represents!
BUT what if a chat has NO forks? Then it only has a root node (and a leaf node). The tree ONLY shows the root node (because `children.size() == 1` nodes are skipped). So the user clicks the root node, and it only shows the first message!
YES! If they click a chat in the tree that has no forks, they click the root node, and it only shows the very first message. Because `tailNodeId` is the root node! It should instead find the leaf node of that path!
"Opening a chat using the tree view should show us the contents of the chat"
If they click a node in the tree view (which could be the root of a chat or a fork), they expect to see the chat up to that point. But wait... what if it's the root of a chat, and they want to see the whole chat? If there are no forks, there's only one path! So we should trace down to the leaf if there are no forks?
Let's trace down to the leaf node if the clicked node has exactly 1 child recursively. If it has 0 children, it IS the leaf. If it has >1 children, it's a fork, so we just show up to the fork? Or we could show up to the fork, and they use the fork explorer to go further. Wait, no! If we click the root, we want to see the latest leaf!
Usually, when you open a chat, you want to see the whole conversation!
Let's trace to the latest leaf? But there could be multiple leaves if there are forks. If they click a root, maybe we should find the most recently modified leaf? Or just any leaf.
Wait, if it's a leaf node in the tree, then `tailNodeId` is the leaf! If it's a fork node, `tailNodeId` is the fork. If it's a root node, `tailNodeId` is the root.
Wait, if I modified `populateMessageForks` to skip nodes with 1 child, then the tree only contains roots, forks, and leaves.
If I click a root node, it will only show up to the root! But wait... the root node IS shown in the tree. And the leaf node IS shown in the tree!
Oh! Because I changed `populateMessageForks` to skip nodes with 1 child, it DOES find the leaf node, and it DOES add it as a "Path: " item!
Wait. If there are NO forks, then `children.size() == 0` for the leaf.
So `populateChatFolders` creates the root node. And then `populateMessageForks` traverses down and creates the leaf node UNDER the root node!
So in the tree, you see:
- Chat 1 (root)
  - Chat 1 (leaf)
But "chat forks are broken. Path: doesn't make sense so should be remove. Fork: Doesn't need to be directly called out."
If we just have a linear chat with no forks, it currently shows:
- Chat 1
  - Chat 1
That is weird. If a chat has NO forks, it shouldn't be a folder! It should just be the chat leaf.
But `populateChatFolders` ALWAYS creates a root node.
Wait. If `populateChatFolders` checks if a root message has any forks recursively, and if not, it just uses the leaf node as the item?
Or better yet, when you click any node in the tree, if it has a linear path down (only 1 child recursively) we can just automatically follow it to the leaf!
Let's trace down to the first fork or leaf!
