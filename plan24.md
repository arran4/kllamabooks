This PR is getting quite large, let's go over these remaining issues.

1. "when I right click on a fork folder I should get the same options (minus new folder) that I do for folders."
- In `showVfsContextMenu` and `showOpenBookContextMenu`, `chat_node` handles should include folder options if it has children?
Wait, if it is a fork folder (i.e. a `chat_node` with children), it is technically a virtual folder.
In `showVfsContextMenu`:
```cpp
    if (currentFolderType == "chats_folder" || currentFolderType == "chat_session" || currentFolderType == "chat_node") {
        newChatAction = menu.addAction(QIcon::fromTheme("chat-message-new"), "New Chat");
        menu.addSeparator();
    }
```
Currently `showOpenBookContextMenu` checks:
```cpp
    } else if (type == "chats_folder" || type == "docs_folder" || type == "notes_folder" || type == "templates_folder" || type == "drafts_folder" || (type == "chat_node" && item->rowCount() > 0)) {
        // ...
```
We need to make sure right-clicking a fork in the tree or VFS gives "New Chat" etc.

2. "The chat prompt contents should be specific to the "chat" I am on, not global"
- This means the input typed in `inputField` / `multiLineInput` should be saved specific to `currentLastNodeId`.
We can use a map `QMap<int, QString> m_chatInputDrafts;` to save the input text for each chat node ID!
When `currentLastNodeId` changes in `updateLinearChatView`:
```cpp
    // Save current
    m_chatInputDrafts[previousNodeId] = inputModeStack->currentIndex() == 0 ? inputField->toPlainText() : multiLineInput->toPlainText();
    // Load new
    QString saved = m_chatInputDrafts.value(currentLastNodeId);
    // ... set it
```

3. "Saving a documents content shouldn't exit it"
- In `saveDocBtn` and `saveNoteBtn` we do:
```cpp
        loadDocumentsAndNotes(); // Refresh tree to show new title
```
`loadDocumentsAndNotes` emits a selection change, which causes `vfsModel` to clear, but wait... `mainContentStack->setCurrentWidget(docContainer);` is still called, so it shouldn't exit it?
Wait, `loadDocumentsAndNotes` emits `selectionChanged`, which triggers `onOpenBooksSelectionChanged`, which does `mainContentStack->setCurrentWidget(docContainer)`. But if we click "Save" on a new document, it does `openBooksTree->setCurrentIndex(newItem->index());` which also triggers it.
Ah! If the title didn't change, `loadDocumentsAndNotes()` completely re-populates the tree, so `openBooksTree` loses its selection, or `currentIndex` becomes invalid, so it switches to `emptyView`!
Yes! `loadDocumentsAndNotes` deletes the items and recreates them! So the selection is lost!
This is related to #10: "Make sure vfs updates and refreshes such as when you open/close a database doesn't cause tree refreshes that cause the user to lose orientation, it should attempt to udpate the tree rather than replace it."

4. "Can we use a toolbar on documents rather than buttons at the bottom"
- Yes, move `saveDocBtn` and `backToDocsBtn` to a `QToolBar` inside `docContainer`, or just a `QHBoxLayout` at the top.

5. "Unsaved documents shoudl become drafts, when you navigate away, and when you navigate back the draft shoudl show plus an indication you're viewing a draft not the original. If there are multiple drafts the user should have a dropdown they can view the original or any of the drafts with. Drafts can be foked off into a new document if desired. via "Save as fork""
- This is a complex new feature. Currently, drafts are just entries in the `drafts` table. The user wants Drafts to be strictly associated with the original Document!
Wait! "If there are multiple drafts the user should have a dropdown".
This requires modifying the database schema to link Drafts to Documents?
Or maybe just relying on the title (`Draft of: ...`)? But a dropdown needs to know which drafts belong to this document.
Wait, `DocumentNode` has no `original_id`. If we parse "Draft of: Title", it's brittle.
If the database doesn't have an `original_id` column, can we just use a combo box that searches for "Draft of: " + `currentTitle` in the `drafts` table?
Yes!

6 & 7 & 10 are all related to `loadDocumentsAndNotes` rebuilding the tree and clearing VFS.
"it should attempt to update the tree rather than replace it."
If we modify `loadDocumentsAndNotes` to UPDATE items instead of `removeRows`, it's a bit of work but doable.
Or simply, only update the changed item!
When we save a document, we only need to `item->setText(currentTitle)`! We DON'T need to call `loadDocumentsAndNotes()`!
```cpp
    item->setText(currentTitle);
```
This entirely avoids the tree rebuild!

Let's address these in smaller patches to avoid a huge mess.
