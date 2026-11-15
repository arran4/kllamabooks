#include <sqlcipher/sqlite3.h>

#include <QTemporaryDir>
#include <QtTest>

#include "../src/BookDatabase.h"
#include "../src/controllers/BookTreeController.h"
#include "../src/db/Database.h"
#include "../src/db/MigrationFactory.h"
#include "../src/store/ArtifactStore.h"
#include "../src/store/ChatStore.h"
#include "../src/store/SearchStore.h"

class TestSearchStore : public QObject {
    Q_OBJECT

   private slots:
    void testMigrationSchemaAndFts5();
    void testIncrementalTriggersAndIdCollisions();
    void testUpsertSearchRemoveRebuildAndSpecialChars();
    void testBookTreeControllerResolveAndSearch();
};

void TestSearchStore::testMigrationSchemaAndFts5() {
    sqlite3* handle = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &handle), SQLITE_OK);
    db::Database db(handle);

    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(db, &err), qPrintable(err));

    int version = 0;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 32);

    int hasFts = 0;
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='search_fts';", hasFts));
    QCOMPARE(hasFts, 1);

    sqlite3_close(handle);
}

void TestSearchStore::testIncrementalTriggersAndIdCollisions() {
    sqlite3* handle = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &handle), SQLITE_OK);
    db::Database db(handle);
    QString err;
    QVERIFY2(db::MigrationFactory::createRunner().run(db, &err), qPrintable(err));

    store::ArtifactStore artStore(db);
    store::ChatStore chatStore(db);
    store::SearchStore searchStore(db);

    // Create Document (id=1), Note (id=2), and ChatSession (id=1) -> numeric ID collision between Document(1) & Chat(1)
    auto doc = artStore.createArtifact(store::ArtifactKind::Document, 5, "Quantum Spec", "Initial photonics draft", "");
    QVERIFY(doc.isSuccess());
    auto note =
        artStore.createArtifact(store::ArtifactKind::Note, 6, "Lab Note", "Superconducting qubit calibration", "");
    QVERIFY(note.isSuccess());
    auto chat = chatStore.createChat(7, "Design Thread", "", "llama3");
    QVERIFY(chat.isSuccess());
    auto msg = chatStore.addMessage(chat.value->id, 0, "user", "Let us benchmark photonics latency");
    QVERIFY(msg.isSuccess());

    // Without calling rebuildIndex(), incremental triggers must have populated FTS5
    const auto photonicsHits = searchStore.search("photonics");
    QCOMPARE(photonicsHits.size(), 2);

    // Update document version content -> FTS5 must reflect updated body and drop old token
    QVERIFY(artStore.editVersion(doc.value->id, "Quantum Spec v2", "Updated graphene waveguide", "").isSuccess());
    QCOMPARE(searchStore.search("graphene", store::ItemKind::Document).size(), 1);
    QCOMPARE(searchStore.search("photonics", store::ItemKind::Document).size(), 0);
    // Chat still matches photonics
    QCOMPARE(searchStore.search("photonics", store::ItemKind::ChatSession).size(), 1);

    // Rename chat -> FTS5 updates title
    QVERIFY(chatStore.renameChat(chat.value->id, "Renamed Photonics Thread").isSuccess());
    const auto renamedHits = searchStore.search("Renamed", store::ItemKind::ChatSession);
    QCOMPARE(renamedHits.size(), 1);
    QCOMPARE(renamedHits.first().item.id, chat.value->id);

    // Delete note -> FTS5 removes it
    QVERIFY(artStore.deleteArtifact(note.value->artifactId).isSuccess());
    QVERIFY(searchStore.search("Superconducting").isEmpty());

    sqlite3_close(handle);
}

void TestSearchStore::testUpsertSearchRemoveRebuildAndSpecialChars() {
    sqlite3* handle = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &handle), SQLITE_OK);
    db::Database db(handle);
    QString err;
    QVERIFY2(db::MigrationFactory::createRunner().run(db, &err), qPrintable(err));

    store::SearchStore searchStore(db);

    const store::ItemRef docRef{store::ItemKind::Document, 42, "documents"};
    const store::ItemRef noteRef{store::ItemKind::Note, 7, "notes"};

    QVERIFY(searchStore.upsertItem(docRef, 11, "Roadmap 2026", "Multi-term encrypted FTS5 search works offline.")
                .isSuccess());
    QVERIFY(searchStore
                .upsertItem(noteRef, 12, "Scratchpad", "Remember to verify foo-bar and \"quoted\" KWallet isolation.")
                .isSuccess());

    // Multi-term + prefix search via FTS5
    const QList<store::SearchHit> multi = searchStore.search("ROADMAP offl");
    QCOMPARE(multi.size(), 1);
    QCOMPARE(multi.first().item.kind, store::ItemKind::Document);
    QCOMPARE(multi.first().item.id, 42);
    QCOMPARE(multi.first().folderId, 11);
    QVERIFY(!multi.first().snippet.isEmpty());

    // Special punctuation / FTS5 operators must not crash or fail
    QCOMPARE(searchStore.search("foo-bar").size(), 1);
    QCOMPARE(searchStore.search("\"quoted\"").size(), 1);

    // Kind filter
    QVERIFY(searchStore.search("kwallet", store::ItemKind::Document).isEmpty());
    QCOMPARE(searchStore.search("kwallet", store::ItemKind::Note).size(), 1);

    // Remove item
    QVERIFY(searchStore.removeItem(docRef).isSuccess());
    QVERIFY(searchStore.search("roadmap").isEmpty());

    // Negative / invalid input
    QVERIFY(!searchStore.upsertItem(store::ItemRef{store::ItemKind::Unknown, 1, ""}, 0, "Bad", "Body").isSuccess());
    QVERIFY(searchStore.search("   ").isEmpty());

    // Rebuild index from underlying tables
    store::ArtifactStore artifactStore(db);
    const auto noteRes =
        artifactStore.createArtifact(store::ArtifactKind::Note, 2, "Rebuilt Note", "Rebuilt body token", "");
    QVERIFY(noteRes.isSuccess());

    const auto rebuildRes = searchStore.rebuildIndex();
    QVERIFY(rebuildRes.isSuccess());
    QVERIFY(*rebuildRes.value >= 1);
    QCOMPARE(searchStore.search("Rebuilt token", store::ItemKind::Note).size(), 1);

    sqlite3_close(handle);
}

void TestSearchStore::testBookTreeControllerResolveAndSearch() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath("search_controller.db");

    BookDatabase bookDb(dbPath);
    QVERIFY(bookDb.open("test_password_pr10"));

    sqlite3* raw = reinterpret_cast<sqlite3*>(bookDb.getDatabaseHandleForTesting());
    QVERIFY(raw != nullptr);
    db::Database db(raw);

    // Create a chat session + message (triggers sync FTS5 automatically!)
    store::ChatStore chatStore(db);
    const auto chatRes = chatStore.createChat(4, "Indexed Chat", "System", "llama3");
    QVERIFY(chatRes.isSuccess());
    const auto msgRes = chatStore.addMessage(chatRes.value->id, 0, "user", "First message about nebula");
    QVERIFY(msgRes.isSuccess());

    const QList<store::SearchHit> hits =
        controllers::BookTreeController::searchBook(&bookDb, "nebula", store::ItemKind::ChatSession);
    QCOMPARE(hits.size(), 1);

    const auto nav = controllers::BookTreeController::resolveNavigation(&bookDb, hits.first().item);
    QCOMPARE(nav.view, controllers::NavigationTargetView::ChatSession);
    QCOMPARE(nav.resolvedNodeId, *msgRes.value);

    // Document navigation resolution
    const auto docNav = controllers::BookTreeController::resolveNavigation(
        &bookDb, store::ItemRef{store::ItemKind::Document, 123, "documents"});
    QCOMPARE(docNav.view, controllers::NavigationTargetView::DocumentEditor);
    QCOMPARE(docNav.resolvedNodeId, 123);

    // Null/invalid safety
    const auto invalidNav = controllers::BookTreeController::resolveNavigation(nullptr, hits.first().item);
    QCOMPARE(invalidNav.view, controllers::NavigationTargetView::None);
    QVERIFY(controllers::BookTreeController::searchBook(nullptr, "nebula").isEmpty());
}

QTEST_MAIN(TestSearchStore)
#include "test_SearchStore.moc"
