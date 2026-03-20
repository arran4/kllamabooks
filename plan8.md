Now, let's look at:
Issue 10: "We need to save the state of "new items" and modified+unsaved items as drafts, until they are saved. (We can still create drafts directly) Once saved drafts no longer save."
Currently, when creating a new doc or note:
```cpp
    QAction* newDocMenuAction = new QAction(QIcon::fromTheme("document-new"), tr("New Document"), this);
    connect(newDocMenuAction, &QAction::triggered, this, [this]() {
        addPhantomItem(nullptr, "docs_folder");
    });
```
And then in `onOpenBooksSelectionChanged`:
```cpp
            currentDocumentId = nodeId;
            isCreatingNewDoc = (nodeId == 0);
            if (isCreatingNewDoc) {
                documentEditorView->clear();
                mainContentStack->setCurrentWidget(docContainer);
            }
```
If you type in `documentEditorView`, it is NOT saved anywhere until you click `saveDocBtn`!
If you navigate away, the `documentEditorView` content is lost!
The user wants: "We need to save the state of "new items" and modified+unsaved items as drafts, until they are saved. (We can still create drafts directly) Once saved drafts no longer save."
Wait, if it saves as a draft, then when you click "Save Document", it should delete the draft and create the actual document (or update the document and delete the draft).
But how does it know a document has an unsaved draft?
Maybe `documentEditorView` `textChanged` signal triggers a draft save?
If `isCreatingNewDoc` or `isCreatingNewNote`, and they type, we create a draft in `currentDb->addDraft(0, "Unsaved Draft", text)`?
But what if they are editing an EXISTING document and haven't saved it?
"modified+unsaved items as drafts"
If they modify an existing document, we save a draft of it.
Wait, if we save a draft, how do they recover it? The user says "(We can still create drafts directly) Once saved drafts no longer save."
This probably means:
If they click "New Document", it creates a "New Document" draft immediately in the Drafts folder?
No, "save the state of new items and modified+unsaved items as drafts, until they are saved".
So if they make a change in a document, it automatically creates/updates a draft in the "Drafts" folder.
When they click "Save", it updates the ACTUAL document, and deletes the draft.
Wait, how do we link a draft to an existing document? `Drafts` table doesn't have an `original_doc_id` column.
Maybe the title of the draft can be `Draft: DocumentTitle`?
Or maybe we can just automatically save it into the Drafts table. But if they reopen the document, does it load the draft or the original?
If they want to recover an unsaved draft, they just open the "Drafts" folder, see the draft there, and can read it?
Yes, "We can still create drafts directly. Once saved drafts no longer save."
So:
When they type in `documentEditorView` (for a doc or template or draft):
Wait, if they are ALREADY editing a draft (e.g. `type == "draft"`), then auto-saving updates that draft!
If they are editing a `document`, we create a draft and auto-save to that draft.
Let's add a QTimer `draftTimer` in `MainWindow`.
When `textChanged` fires in `documentEditorView` or `noteEditorView` or `chatTextArea` (for edits):
Wait, chat history has an auto-save because they said "Discard Changes" discards them. If they edit a chat, it shows "Discard Changes". Should chat edits be saved as drafts? "new items and modified+unsaved items".
Usually documents, notes, templates.
Let's see what happens if they click "Save Document".
```cpp
    connect(saveDocBtn, &QPushButton::clicked, this, [this]() {
        // ...
        if (isCreatingNewDoc && item) {
             // ... adds document
             currentDb->addDocument(folderId, currentTitle, documentEditorView->toPlainText());
             // delete draft if exists
        }
    });
```
