Wait!
"We need to save the state of new items and modified+unsaved items as drafts, until they are saved. (We can still create drafts directly) Once saved drafts no longer save."
If the user opens an existing document and edits it, we should automatically save those edits to the database (in the Drafts table) and then when they click "Save", it goes to the real table and deletes the draft.
But what if they don't click Save, and they just close the app?
When they open the app again, do they see their Draft in the tree? Yes!
But wait, if they have the "Document" in the tree, and the "Draft" in the tree...
If they open the Document, they see the old version? If they open the Draft, they see the new version?
This is fine! "We can still create drafts directly". "Once saved drafts no longer save" -> maybe it just means when you save a draft as a document, the draft is removed.
Let's just implement an auto-save that writes a draft.
Actually, if `isCreatingNewDoc` is true, we should create a new draft immediately.
But wait, when they do `addPhantomItem(nullptr, "docs_folder");` it creates an item in the tree that is a phantom `*New Item*`. It DOES NOT create a database entry!
When they click it, `currentDocumentId = 0` and `isCreatingNewDoc = true`.
If they type, we can create a Draft in the database! And then update the tree item so it's NO LONGER a phantom item, but an actual `draft` item!
Wait! If we update the tree item to be a `draft`, then it moves to the `drafts_folder`?
That would be a jarring UI experience if it disappears from where they created it and moves to the Drafts folder.
The easiest way:
1. "new items and modified+unsaved items as drafts".
Maybe they mean Drafts are a specific type of node.
If you click "New Document", it adds a phantom document. You type. It gets saved as a draft?
No, we should just save it as a draft implicitly?
Wait, look at `saveDocBtn` again.
