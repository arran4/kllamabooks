Let's add `int currentDraftId = 0;` to `MainWindow.h`.
When a user opens a document or note or creates a new one, `currentDraftId = 0;`.
When `textChanged` fires in `documentEditorView` or `noteEditorView`:
Wait, we need to know what they are editing!
If `isCreatingNewDoc` or `currentDocumentId != 0`:
```cpp
    connect(documentEditorView, &QTextEdit::textChanged, this, [this]() {
        if (!currentDb || !documentEditorView->hasFocus()) return;
        // The user is typing...
        if (currentDraftId == 0) {
            QString title = "Draft of ";
            if (isCreatingNewDoc) title += "New Document";
            else {
                // find document title
            }
            currentDraftId = currentDb->addDraft(0, title, documentEditorView->toPlainText());
            loadDocumentsAndNotes(); // This might steal focus, so only call it if we use a timer!
        } else {
            // Update the draft
            // If we use a QTimer, it won't stutter the UI.
        }
    });
```
Actually, we can use a `QTimer` `draftSaveTimer`!
In `MainWindow::setupUi`:
```cpp
    QTimer* draftTimer = new QTimer(this);
    draftTimer->setSingleShot(true);
    draftTimer->setInterval(1000);
    connect(documentEditorView, &QTextEdit::textChanged, this, [draftTimer, this]() {
        if (documentEditorView->hasFocus()) draftTimer->start();
    });
    connect(noteEditorView, &QTextEdit::textChanged, this, [draftTimer, this]() {
        if (noteEditorView->hasFocus()) draftTimer->start();
    });
```
Wait, if they edit a chat, the chat has a "Discard Changes" button. Should that also be drafted?
"save the state of new items and modified+unsaved items as drafts, until they are saved".
When the user clicks "Discard Changes" it reverts the chat edits.
If they are editing a chat, it sets `discardChangesBtn->show()`.
What if they navigate away from the chat? The edits in `chatTextArea` are lost, because `updateLinearChatView` clears it.
If we save a draft of the chat, how does it reload?
Currently, if they right-click the chat, they can do "Save chat as draft" explicitly.
"We need to save the state of new items and modified+unsaved items as drafts, until they are saved. (We can still create drafts directly) Once saved drafts no longer save."
This probably means: when creating a new doc, a draft is created automatically. If they save the doc, the draft is removed. If they don't save the doc and close the app, the draft remains!
Wait, if they just create a new item, it's immediately put into the `Drafts` table until they click "Save"!
Yes! "We need to save the state of new items and modified+unsaved items as drafts, until they are saved."
So instead of having `currentDraftId`, if they click "New Document", it literally creates a draft in the database, opens it as a draft, but remembers it was supposed to be a document?
No, the easiest way:
1. When they modify an existing document/note, we start a timer to auto-save it to a NEW draft.
2. When they click "Save", it updates the original document and deletes the draft!
3. If they create a "New Document", it just creates a draft immediately, and sets `currentDraftId` to that. When they click "Save", it deletes the draft and creates the actual Document.
Wait, if they create a "New Document" in the UI (e.g. `addPhantomItem`), it creates a phantom item in the tree. It doesn't actually create a DB entry until "Save".
But if they type in it, we should automatically save a draft in the background!
