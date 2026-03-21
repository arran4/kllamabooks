Wait! In `onOpenBooksSelectionChanged`, for notes:
```cpp
        } else if (type == "note") {
            currentNoteId = nodeId;
            isCreatingNewNote = (nodeId == 0);
            if (isCreatingNewNote) {
                noteEditorView->clear();
                mainContentStack->setCurrentWidget(noteContainer);
            } else {
                // For notes, we just use the selected item's text if possible,
                // but let's fetch content from DB to be sure
                if (currentDb) {
                    QList<NoteNode> notes = currentDb->getNotes();
                    for (const auto& note : notes) {
                        if (note.id == currentNoteId) {
                            noteEditorView->setPlainText(note.content);
                            mainContentStack->setCurrentWidget(noteContainer);
                            break;
                        }
                    }
                }
            }
```
If we open a note, we set `noteEditorView->setPlainText(note.content);`. Then we click Save Note:
```cpp
        if (isCreatingNewNote && item) {
             // ...
             int newId = currentDb->addNote(folderId, currentTitle, noteEditorView->toPlainText());
             // ...
             return;
        }
        if (currentNoteId == 0) return;
        currentDb->updateNote(currentNoteId, currentTitle, noteEditorView->toPlainText());
        // ...
```
This looks perfectly fine. BUT wait! The auto-draft timer!
When we open a note and edit it, the `draftTimer` starts. And when it saves:
```cpp
        if (currentAutoDraftId == 0) {
            currentAutoDraftId = currentDb->addDraft(0, title, documentEditorView->hasFocus() ? documentEditorView->toPlainText() : noteEditorView->toPlainText());
        } else {
            currentDb->updateDraft(currentAutoDraftId, title, documentEditorView->hasFocus() ? documentEditorView->toPlainText() : noteEditorView->toPlainText());
        }
```
Wait! `documentEditorView->hasFocus()` ? If the user clicks out, it loses focus!
If they click somewhere else and the timer fires, `hasFocus()` might be false for both!
And then it saves `noteEditorView->toPlainText()` because the condition is false! But they were editing `documentEditorView`!
THIS is why Note saving/opening is inconsistent! The draft saves the wrong content!
If I type in `documentEditorView`, click on a folder in the tree, the timer fires. `documentEditorView` no longer has focus. It saves `noteEditorView->toPlainText()` to the draft!
And then if they click "Save Note", it uses `noteEditorView->toPlainText()`.

We shouldn't use `hasFocus()` in the timer. We should know WHICH editor is currently active!
We can just check which one is visible! `mainContentStack->currentWidget() == docContainer` vs `noteContainer`.
Yes!

Also, 1. "tree view drag drop works. However The drag drops I do inside the "central widget's file explorer list view" don't persist / apply"
Let's fix the `QEvent::Drop` targetItem extraction for VFS drops.
