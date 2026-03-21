Wait, look at `BookDatabase::moveItem` again.
```cpp
    QString sql = QString("UPDATE %1 SET folder_id = ? WHERE id = ?;").arg(safeTable);
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, newFolderId);
    sqlite3_bind_int(stmt, 2, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
```
Wait. "I have 1 folder 'Folder' and 1 file 'File' in Documents."
If they drag 'File' to 'Folder', `newFolderId` is the ID of 'Folder'.
What if `targetFolderId` is retrieved incorrectly in `moveItemToFolder`?
```cpp
    QString targetType = targetItem->data(Qt::UserRole + 1).toString();
    int targetFolderId = targetItem->data(Qt::UserRole).toInt();

    if (!targetType.endsWith("_folder")) {
        if (targetItem->parent()) {
            targetItem = targetItem->parent();
            targetType = targetItem->data(Qt::UserRole + 1).toString();
            targetFolderId = targetItem->data(Qt::UserRole).toInt();
        } else {
            return false;
        }
    }
```
If 'Folder' is a `docs_folder`, its ID is `targetFolderId`.
And the DB updates `folder_id = targetFolderId`.
BUT WAIT!
If I create a new folder "Folder" in "Documents", what is its `parent_id`?
Its `parent_id` is 0 (because the root "Documents" item has ID 0).
Its `type` is `"documents"`.
Its `name` is `"Folder"`.
It is stored in the `folders` table!
```cpp
            int newFId = currentDb->addFolder(parentId, name, fType);
```
If I drag a Document into this Folder, its `folder_id` is updated to `newFId` in the `documents` table!
This is absolutely correct!
If it's absolutely correct, why doesn't it persist?
Could the database be closing without saving? No, `sqlite3_step` writes to the WAL/journal or directly to the DB file depending on PRAGMAs. SQLCipher defaults to syncing.
Wait... what if `targetType` is "docs_folder", but `targetFolderId` is 0?
Yes! The root "Documents" folder has `targetFolderId == 0`.
If I move a file to "Documents", `newFolderId` = 0.
`db->moveItem` sets `folder_id = 0`.
This also works!

Wait... "When I do it from the tree it updates in the tree and for the file explorer. However neither changes persist when I restart the application"
Wait!!!
Look at `populateDocumentFolders`!
```cpp
    // First, add subfolders
    QList<FolderNode> folders = db->getFolders(type);
    for (const auto& folder : folders) {
        if (folder.parentId == folderId) {
            QStandardItem* item = new QStandardItem(QIcon::fromTheme("folder-open"), folder.name);
            item->setData(folder.id, Qt::UserRole);
            QString folderTypeSuffix = "_folder";
            if (type == "documents") item->setData("docs_folder", Qt::UserRole + 1);
            // ...
            parentItem->appendRow(item);
            populateDocumentFolders(item, folder.id, type, db);
        }
    }
```
Wait! `populateDocumentFolders(item, folder.id, type, db);`
When recursing into subfolders, `type` is STILL `"documents"`!
And it queries `db->getDocuments(folderId)`:
```cpp
    if (type == "documents" || type == "templates" || type == "drafts") {
        QList<DocumentNode> docs;
        if (type == "documents") docs = db->getDocuments(folderId);
        // ...
        for (const auto& doc : docs) {
            // append to parentItem
        }
    }
```
Wait! `db->getDocuments(folderId)`!
Let's look at `db->getDocuments`!
