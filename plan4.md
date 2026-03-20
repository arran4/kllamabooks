For Issue 3: "chat forks are broken. Path: doesn't make sense so should be remove. Fork: Doesn't need to be directly called out. The fork represents represents the part of the conversation before the fork began, then 2 new conversations are in effect created as sub children. (More can be) this isn't currently how it's behaving."
and Issue 7: "A fork always uses the fork icon"
and Issue 9: "Forks are virtual folders and should be openable, if they aren't then they aren't a fork."

Let's modify `populateMessageForks` and `populateChatFolders`.

```cpp
            QStandardItem* item = nullptr;
            if (children.size() > 1) {
                item = new QStandardItem(QIcon::fromTheme("vcs-branch", QIcon::fromTheme("folder-open")), "Fork: " + displayTitle);
            } else if (children.size() == 0) {
                item = new QStandardItem(QIcon::fromTheme("text-x-generic"), "Path: " + displayTitle);
            }
```

If it has children.size() > 1, it is a fork. The text should just be `displayTitle`, icon `vcs-branch`. Wait, what if it has 1 child? Then it's just a regular message in the chain!
Wait! "The fork represents represents the part of the conversation before the fork began, then 2 new conversations are in effect created as sub children. (More can be) this isn't currently how it's behaving."
Currently, `populateChatFolders` adds root messages (sessions) to the chats_folder.
And then `populateMessageForks` recursively adds ALL messages that have children... wait.
If `children.size() > 1`, it creates a fork node.
If `children.size() == 0`, it creates a path node (leaf node).
If `children.size() == 1`, it DOESN'T create an item, but it recursively calls `populateMessageForks(item, msg.id, allMessages)` with `item` from above?
Wait, if `children.size() == 1`, `item` is `nullptr`. So `if (item)` is false, it doesn't append an item, and DOES NOT recursively call `populateMessageForks`!
Wait, if it doesn't call `populateMessageForks(item, msg.id, allMessages)`, how does it traverse the chain?
Ah! If `children.size() == 1`, `item` is null, so it just skips this node and it doesn't process its children?!
Wait, NO! The outer loop iterates over ALL messages!
```cpp
void MainWindow::populateMessageForks(QStandardItem* parentItem, int parentId, const QList<MessageNode>& allMessages) {
    for (const auto& msg : allMessages) {
        if (msg.parentId == parentId) {
            // Find all children of this message
            QList<MessageNode> children;
            for (const auto& childMsg : allMessages) {
                if (childMsg.parentId == msg.id) {
                    children.append(childMsg);
                }
            }
            // ... creates item or not ...
            if (item) {
                item->setData(msg.id, Qt::UserRole);
                item->setData("chat_node", Qt::UserRole + 1);
                parentItem->appendRow(item);
                // Recursively populate under this item.
                populateMessageForks(item, msg.id, allMessages);
            }
        }
    }
}
```
If `item` is null (because `children.size() == 1`), it doesn't append anything, AND it DOES NOT recursively call `populateMessageForks`! BUT wait! If it doesn't recursively call it, how do the children ever get added? The outer loop only checks `msg.parentId == parentId`. If `parentId` is the root node, it finds the root's children. If the root has 1 child, it doesn't create an item, and it doesn't call `populateMessageForks`. So it stops!
YES! That is why chat forks are completely broken! If a node has exactly 1 child, the traversal STOPS!
Wait, wait. If it has 1 child, it should NOT be added as an item in the tree view (because we only want to show forks and leaves), BUT we MUST continue traversing down the chain to find forks and leaves!
So if `children.size() == 1`, we should just call `populateMessageForks(parentItem, msg.id, allMessages)`!
