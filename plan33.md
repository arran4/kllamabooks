Wait, why does `moveItemToFolder` fail to persist?
Ah!
`table = "documents";`
`db->moveItem(table, itemId, targetFolderId)`
If `db->moveItem` executes correctly, it modifies the database!
Then it calls `loadDocumentsAndNotes()`.
And it returns `true`.
Why does the user say it DOES NOT UPDATE THE TREE?
"I drag and drop it using the list view file explorer which si the central widget, it doesn't persist, or update the tree. When I do it from the tree it updates in the tree and for the file explorer."
If the user does it from the TREE, it updates BOTH the tree and the VFS! And it persists?!
Wait! "However neither changes persist when I restart the application, these changes need a consistent internal state that is updated once and broadcast out and updated in teh database too."
"However neither changes persist when I restart the application"
If dragging in the tree updates the tree and VFS, `moveItemToFolder` returned `true` AND `loadDocumentsAndNotes` successfully reloaded it!
If `loadDocumentsAndNotes` successfully reloaded it, then `db->getDocuments()` returned it with the NEW `folder_id`!
If `db->getDocuments()` returned it with the NEW `folder_id`, the database object MUST have the new `folder_id`!
Why would it NOT persist when restarting the application?!
Is the database NOT writing to disk?!
SQLite ALWAYS writes to disk unless it's an in-memory database or inside a transaction that is never committed.
Are we inside a transaction? No.
Let's check `BookDatabase::moveItem` again.
