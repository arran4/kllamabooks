We need to check if the target element exists in the DB before jumping.

For "document":
```cpp
bool isValid = false;
auto docs = mi.db->getDocuments(-1);
for (const auto& doc : docs) {
    if (doc.id == mi.item.messageId) {
        isValid = true;
        break;
    }
}
```

For "message":
```cpp
bool isValid = false;
int rootId = mi.db->getRootMessageId(mi.item.messageId);
// But how do we know if the message itself actually exists?
// getRootMessageId returns currentId if it fails to find the parent.
// Wait, we can check if getChat(rootId).title is not empty?
// Or better, we can query if the message exists via `db->getMessages()` but that's a tree.
// Is there a way to check if a message exists?
```

Let's check `src/BookDatabase.cpp` to see if there's a simple way to check if a message exists.
