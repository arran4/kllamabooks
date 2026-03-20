Ah! When it's `isCreatingNewDoc`, it calls `addDocument()`. If the user created a *Template*, it STILL calls `addDocument`?
Wait! `addPhantomItem` sets `itemType` based on `type`.
```cpp
    QString itemType = type;
    if (type == "chats_folder") itemType = "chat_session";
    else if (type == "docs_folder") itemType = "document";
    else if (type == "templates_folder") itemType = "template";
    else if (type == "drafts_folder") itemType = "draft";
    else if (type == "notes_folder") itemType = "note";
```
Then in `onOpenBooksSelectionChanged`:
```cpp
        if (type == "document" || type == "template" || type == "draft") {
            currentDocumentId = nodeId;
            isCreatingNewDoc = (nodeId == 0);
```
So `isCreatingNewDoc` is true for ANY of those three types!
And in `saveDocBtn`:
```cpp
        if (isCreatingNewDoc && item) {
             // wait, it calls currentDb->addDocument! For templates AND drafts too?
```
Yes! That's a huge bug! If I create a New Template, type something, and click Save, it creates a Document instead of a Template!
Let's fix `saveDocBtn` first.
```cpp
            int newId = 0;
            QString type = item->data(Qt::UserRole + 1).toString();
            if (type == "document") newId = currentDb->addDocument(folderId, currentTitle, documentEditorView->toPlainText());
            else if (type == "template") newId = currentDb->addTemplate(folderId, currentTitle, documentEditorView->toPlainText());
            else if (type == "draft") newId = currentDb->addDraft(folderId, currentTitle, documentEditorView->toPlainText());
            else newId = currentDb->addDocument(folderId, currentTitle, documentEditorView->toPlainText());
```
And similarly for `updateDocument`:
```cpp
        if (currentDocumentId == 0) return;
        QString type = item ? item->data(Qt::UserRole + 1).toString() : "document";
        if (type == "document") currentDb->updateDocument(currentDocumentId, currentTitle, documentEditorView->toPlainText());
        else if (type == "template") currentDb->updateTemplate(currentDocumentId, currentTitle, documentEditorView->toPlainText());
        else if (type == "draft") currentDb->updateDraft(currentDocumentId, currentTitle, documentEditorView->toPlainText());
```

Now, what about auto-saving drafts?
Let's add a QTimer `draftTimer`. When it fires, if `isCreatingNewDoc` OR we are editing an existing item, we auto-save it.
Wait! "We need to save the state of "new items" and modified+unsaved items as drafts, until they are saved. (We can still create drafts directly) Once saved drafts no longer save."
This probably means: when you start typing a NEW item or modifying an EXISTING item, it creates a Draft object in the DB.
When you click Save, it saves the actual document and deletes the auto-draft.
To implement:
`int currentAutoDraftId = 0;` in `MainWindow.h`.
```cpp
    QTimer* draftTimer = new QTimer(this);
    draftTimer->setSingleShot(true);
    draftTimer->setInterval(2000);
    connect(documentEditorView, &QTextEdit::textChanged, this, [draftTimer, this]() {
        if (documentEditorView->hasFocus()) draftTimer->start();
    });
    connect(draftTimer, &QTimer::timeout, this, [this]() {
        if (!currentDb) return;
        QModelIndex index = openBooksTree->currentIndex();
        QStandardItem* item = index.isValid() ? openBooksModel->itemFromIndex(index) : nullptr;
        if (!item) return;

        QString title = item->text();
        if (title == "*New Item*") title = "Unsaved Draft";
        else title = "Draft of: " + title;

        if (currentAutoDraftId == 0) {
            currentAutoDraftId = currentDb->addDraft(0, title, documentEditorView->toPlainText());
            loadDocumentsAndNotes(); // This updates the UI so the draft shows up
        } else {
            currentDb->updateDraft(currentAutoDraftId, title, documentEditorView->toPlainText());
        }
    });
```
And then in `saveDocBtn`:
```cpp
        // After successfully saving...
        if (currentAutoDraftId != 0) {
            currentDb->deleteDraft(currentAutoDraftId);
            currentAutoDraftId = 0;
            loadDocumentsAndNotes();
        }
```
Wait! What if they click "New Draft", which creates a Draft? We don't want to auto-draft a draft!
If `type == "draft"`, we just auto-save to the draft itself!
```cpp
        QString type = item->data(Qt::UserRole + 1).toString();
        if (type == "draft") {
            if (currentDocumentId != 0) {
                currentDb->updateDraft(currentDocumentId, title, documentEditorView->toPlainText());
            } else {
                currentDocumentId = currentDb->addDraft(0, title, documentEditorView->toPlainText());
                isCreatingNewDoc = false;
                loadDocumentsAndNotes();
                // Find and select it
                QStandardItem* newItem = findItemInTree(currentDocumentId, "draft");
                if (newItem) openBooksTree->setCurrentIndex(newItem->index());
            }
            return;
        }
```
This satisfies "We can still create drafts directly. Once saved drafts no longer save." (Wait, if it's a draft, clicking "Save" just saves it. Does it no longer save?)
If they click Save on a Draft, does it convert it to a Document? No, it's just a Draft.
But auto-saving modifications to a Document creates an auto-draft. Once they click "Save Document", the document is updated, and the auto-draft is deleted ("Once saved drafts no longer save").
This is perfectly fitting!

Wait, let's also do it for Notes!
