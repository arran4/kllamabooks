Wait, if they double click a node with `type == "draft"`, what happens?
`onOpenBooksSelectionChanged` handles `type == "draft"`, sets `currentDocumentId`, `isCreatingNewDoc = (nodeId == 0)`, and sets it in `documentEditorView`.
If they make changes, they click "Save Document", and `saveDocBtn` saves it as a draft (since `type == "draft"`).
This perfectly matches "We can still create drafts directly. Once saved drafts no longer save."
Wait, "Once saved drafts no longer save."? Does it mean that when you save it, it stops being a draft?
But if you are actively editing a "Draft" item from the Drafts folder, clicking "Save Document" just saves the draft. Is that intended? If they want it to become a document, they'd have to move it to the Documents folder?
Oh! Drag and Drop! They can just drag the draft from Drafts to Documents!
And wait... `MainWindow::moveItemToFolder` does exactly that!
```cpp
    bool compatible = false;
    QString table;
    if (itemType == "document" && targetType == "docs_folder") { table = "documents"; compatible = true; }
    else if (itemType == "template" && targetType == "templates_folder") { table = "templates"; compatible = true; }
    ...
```
Wait! In `moveItemToFolder`, if I drag a `draft` to `docs_folder`, it won't work! Because `compatible` is false!
Ah! "Once saved drafts no longer save" -> maybe it literally means that when you save a Draft, it automatically moves out of Drafts?
No, "save the state of new items and modified+unsaved items as drafts, until they are saved. (We can still create drafts directly) Once saved drafts no longer save."
If I create an auto-draft of a modified document, the auto-draft is deleted when I save the document! "Once saved, drafts no longer save" -> maybe they mean the auto-saving of drafts stops?
Or maybe it means "the draft is deleted when the document is saved."

Now we need to do it for chat history edits!
When the user edits chat history, "discardChangesBtn" shows up.
Should we auto-save chat edits to a draft?
"new items and modified+unsaved items"
Usually this means documents. I think the timer and auto-draft mechanism is exactly what is needed.
Wait, let's fix `onOpenBooksSelectionChanged` to reset `currentAutoDraftId = 0`.
When you click a different node in the tree, we should reset `currentAutoDraftId = 0`!
