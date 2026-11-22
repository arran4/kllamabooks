#include <sqlcipher/sqlite3.h>

#include <QStandardItem>
#include <QtTest>

#include "../src/BookDatabase.h"
#include "../src/db/Database.h"
#include "../src/db/MigrationFactory.h"
#include "../src/store/ArtifactStore.h"
#include "../src/store/FolderStore.h"
#include "../src/store/ItemRef.h"
#include "../src/store/RecoveryStore.h"

class TestFolderStore : public QObject {
    Q_OBJECT

   private slots:
    void testFolderHierarchyAndCycleRejection();
    void testMoveItemsAcrossFoldersWithItemRef();
    void testDeleteFolderReparentsChildrenAndItems();
    void testItemRefStandardItemRoundTrip();
    void testMigration27To28NormalizesAndQuarantinesOrphanAndCyclicFolders();
};

void TestFolderStore::testFolderHierarchyAndCycleRejection() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(database, &err), qPrintable(err));

    store::FolderStore folderStore(database);
    auto rootRes = folderStore.createFolder(0, "Root Folder", "documents", true);
    QVERIFY(rootRes.isSuccess());
    int rootId = *rootRes.value;

    auto childRes = folderStore.createFolder(rootId, "Child Folder", "documents", false);
    QVERIFY(childRes.isSuccess());
    int childId = *childRes.value;

    auto grandChildRes = folderStore.createFolder(childId, "Grandchild Folder", "documents", false);
    QVERIFY(grandChildRes.isSuccess());
    int grandChildId = *grandChildRes.value;

    // Self-parenting must be rejected
    auto selfMove = folderStore.moveFolder(rootId, rootId);
    QVERIFY(!selfMove.isSuccess());
    QCOMPARE(selfMove.error, store::TransitionError::Conflict);

    // Moving ancestor into child or grandchild must be rejected
    auto childMove = folderStore.moveFolder(rootId, childId);
    QVERIFY(!childMove.isSuccess());
    QCOMPARE(childMove.error, store::TransitionError::Conflict);

    auto grandChildMove = folderStore.moveFolder(rootId, grandChildId);
    QVERIFY(!grandChildMove.isSuccess());
    QCOMPARE(grandChildMove.error, store::TransitionError::Conflict);

    // Moving grandchild to root (0) or directly under rootId is valid
    auto validMove = folderStore.moveFolder(grandChildId, rootId);
    QVERIFY(validMove.isSuccess());
    auto movedOpt = folderStore.getFolder(grandChildId);
    QVERIFY(movedOpt.has_value());
    QCOMPARE(movedOpt->parentId, rootId);

    sqlite3_close(rawDb);
}

void TestFolderStore::testMoveItemsAcrossFoldersWithItemRef() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    int docFolder = db.addFolder(0, "Docs Folder", "documents");
    int noteFolder = db.addFolder(0, "Notes Folder", "notes");
    int chatFolder = db.addFolder(0, "Chats Folder", "chats");
    QVERIFY(docFolder > 0);
    QVERIFY(noteFolder > 0);
    QVERIFY(chatFolder > 0);

    int docId = db.addDocument(0, "Doc 1", "Body 1");
    int noteId = db.addNote(0, "Note 1", "Note Body");
    int msgId = db.addMessage(0, "Hello chat", "user", 0);
    QVERIFY(docId > 0);
    QVERIFY(noteId > 0);
    QVERIFY(msgId > 0);

    QVERIFY(db.moveItem({store::ItemKind::Document, docId, "documents"}, docFolder));
    QVERIFY(db.moveItem({store::ItemKind::Note, noteId, "notes"}, noteFolder));
    QVERIFY(db.moveItem({store::ItemKind::ChatSession, msgId, "chats"}, chatFolder));

    QCOMPARE(db.getDocument(docId)->folderId, docFolder);
    QCOMPARE(db.getNote(noteId)->folderId, noteFolder);
    QCOMPARE(db.getMessage(msgId)->folderId, chatFolder);
}

void TestFolderStore::testDeleteFolderReparentsChildrenAndItems() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    int parentFolder = db.addFolder(0, "Parent", "documents");
    int midFolder = db.addFolder(parentFolder, "Middle", "documents");
    int leafFolder = db.addFolder(midFolder, "Leaf", "documents");
    int docId = db.addDocument(midFolder, "Doc in Middle", "Content");

    QVERIFY(db.deleteFolder(midFolder));

    // Leaf folder and document should be re-parented to parentFolder rather than orphaned
    auto folders = db.getFolders("documents");
    bool foundLeaf = false;
    for (const auto& f : folders) {
        if (f.id == leafFolder) {
            QCOMPARE(f.parentId, parentFolder);
            foundLeaf = true;
        }
    }
    QVERIFY(foundLeaf);
    QCOMPARE(db.getDocument(docId)->folderId, parentFolder);
}

void TestFolderStore::testItemRefStandardItemRoundTrip() {
    QStandardItem item("My Doc");
    store::ItemRef expected{store::ItemKind::Document, 42, QStringLiteral("documents")};
    store::applyItemRef(&item, expected);

    QCOMPARE(item.data(Qt::UserRole).toInt(), 42);
    QCOMPARE(item.data(Qt::UserRole + 1).toString(), QString("document"));

    store::ItemRef actual = store::itemRefFromStandardItem(&item);
    QCOMPARE(actual, expected);
}

void TestFolderStore::testMigration27To28NormalizesAndQuarantinesOrphanAndCyclicFolders() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);

    db::MigrationRunner fullRunner = db::MigrationFactory::createRunner();
    db::MigrationRunner runnerTo27;
    const db::Migration* mig27To28 = nullptr;
    for (const auto& m : fullRunner.getMigrations()) {
        if (m.toVersion <= 27) {
            runnerTo27.addMigration(m);
        } else if (m.fromVersion == 27 && m.toVersion == 28) {
            mig27To28 = &m;
        }
    }
    QVERIFY(mig27To28 != nullptr);
    QString err;
    QVERIFY2(runnerTo27.run(database, &err), qPrintable(err));

    // Seed legacy folder type='folder', an orphaned folder (parent_id=999), and a 2-node folder cycle (10 <-> 11)
    QVERIFY(
        database.execute("INSERT INTO folders (id, parent_id, name, type) VALUES (1, 0, 'Legacy Folder', 'folder');"
                         "INSERT INTO folders (id, parent_id, name, type) VALUES (2, 999, 'Orphan Folder', 'notes');"
                         "INSERT INTO folders (id, parent_id, name, type) VALUES (10, 11, 'Cycle A', 'documents');"
                         "INSERT INTO folders (id, parent_id, name, type) VALUES (11, 10, 'Cycle B', 'documents');"));

    db::MigrationRunner runner28;
    runner28.addMigration(*mig27To28);
    QVERIFY2(runner28.run(database, &err), qPrintable(err));

    store::FolderStore folderStore(database);
    auto f1 = folderStore.getFolder(1);
    QVERIFY(f1.has_value());
    QCOMPARE(f1->rootType, QString("documents"));

    auto f2 = folderStore.getFolder(2);
    QVERIFY(f2.has_value());
    QCOMPARE(f2->parentId, 0);

    // Cycle between 10 and 11 must be broken without losing either folder
    QVERIFY(!folderStore.wouldCreateCycle(10, 11) || !folderStore.wouldCreateCycle(11, 10));

    store::RecoveryStore recoveryStore(database);
    QString recErr;
    auto quarantined = recoveryStore.getRecords(QString(), "folders", &recErr);
    QVERIFY2(recErr.isEmpty(), qPrintable(recErr));
    QVERIFY(quarantined.size() >= 2);

    sqlite3_close(rawDb);
}

QTEST_MAIN(TestFolderStore)
#include "test_FolderStore.moc"
