Let's see `populateChatFolders`:
```cpp
    // 2. Add root messages (sessions) that belong to this folder
    for (const auto& msg : allMessages) {
        if (msg.parentId == 0 && msg.folderId == folderId) {
            // Determine children to show the correct icon/label
            QList<MessageNode> children;
            for (const auto& childMsg : allMessages) {
                if (childMsg.parentId == msg.id) {
                    children.append(childMsg);
                }
            }
```
If we find a root message, what is the fork logic?
"The fork represents the part of the conversation before the fork began, then 2 new conversations are in effect created as sub children. (More can be) this isn't currently how it's behaving."
Wait, if a chat has no forks, it's just a sequence of messages. The root shouldn't be a folder containing leaves. It should just be ONE item in the tree representing the chat. Its `tailNodeId` should be the leaf node!
But wait, how do we show a chat? A chat in the tree should point to its leaf node!
If it forks, then the fork node points to the fork message, and its children point to their respective leaves.
So in `populateChatFolders`, for each root message, we shouldn't just create an item for the root. We should trace down the single-child chain until we hit a fork or a leaf.
Let's make a helper function `int findEndOfLinearPath(int startId, const QList<MessageNode>& allMessages, QList<MessageNode>& childrenOut)`
It starts at `startId`. Finds its children. If it has exactly 1 child, it goes to that child and repeats.
If it has 0 children or >1 children, it stops and returns that node ID, and sets `childrenOut` to the children of that node.
Then, `populateChatFolders` creates an item for THAT end node!
And then it recurses `populateMessageForks` on that item!
And in `populateMessageForks`, for each child, we find the end of ITS linear path, create an item for that end node, and recurse!
This perfectly matches "The fork represents the part of the conversation before the fork began, then 2 new conversations are in effect created as sub children."!
YES! Every item in the tree is either a leaf (0 children) or a fork point (>1 children)! A linear chain is squashed into a single edge!
And its title is derived from the first user message in that segment, or something.
This is exactly how chat histories in modern LLM UIs work.

Let's write `int getEndOfLinearPath(int startId, const QList<MessageNode>& allMessages, QList<MessageNode>& outChildren)`
```cpp
int MainWindow::getEndOfLinearPath(int startId, const QList<MessageNode>& allMessages, QList<MessageNode>& outChildren) {
    int currentId = startId;
    while (true) {
        outChildren.clear();
        for (const auto& msg : allMessages) {
            if (msg.parentId == currentId) {
                outChildren.append(msg);
            }
        }
        if (outChildren.size() == 1) {
            currentId = outChildren.first().id;
        } else {
            break;
        }
    }
    return currentId;
}
```
If we do this, `populateChatFolders` will just call `getEndOfLinearPath` on the root message.
But wait! If `populateChatFolders` does that, the tree item will have the ID of the END of the path (the leaf or the fork point).
When we click that item, `tailNodeId` is the end of the path! So `updateLinearChatView` will show the entire chat up to that point!
This perfectly fixes "Currently opening a chat isn't showing the contents, we expect to see the chat contents when we open it" AND "Forks are virtual folders and should be openable".

Let's modify `MainWindow.h` to add `int getEndOfLinearPath(int startId, const QList<MessageNode>& allMessages, QList<MessageNode>& outChildren);`
And `MainWindow.cpp` to use it in `populateChatFolders` and `populateMessageForks`.
