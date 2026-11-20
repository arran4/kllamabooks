#include <sqlcipher/sqlite3.h>

#include <QtTest>

#include "../src/BookDatabase.h"
#include "../src/db/Database.h"
#include "../src/db/MigrationFactory.h"
#include "../src/store/ArtifactStore.h"
#include "../src/store/PromptStore.h"
#include "../src/store/RecoveryStore.h"
#include "../src/store/RunStore.h"

class TestRunStore : public QObject {
    Q_OBJECT

   private slots:
    void testAtomicClaimLeaseAndDeterministicRecovery();
    void testDescendantRunPreservesOriginalHistory();
    void testOptimisticApplyConflictDetection();
    void testApplyRunOutputAtomicRollback();
    void testDeleteQueueItemPreservesRunHistory();
    void testMigration28To29BackfillsQueueHistoryMergesAndQuarantinesNulls();
};

void TestRunStore::testAtomicClaimLeaseAndDeterministicRecovery() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(database, &err), qPrintable(err));

    store::PromptStore promptStore(database);
    auto rev = promptStore.createRevision("user", "", "document", 1, "", "Hello", "llama3", true);
    QVERIFY(rev.isSuccess());

    store::RunStore runStore(database);
    auto runRes = runStore.enqueueRun(rev.value->id, "document", 1, 0, "complete", "llama3", 5);
    QVERIFY(runRes.isSuccess());
    const int runId = runRes.value->id;

    // First worker claims successfully; second worker gets Conflict
    auto claim1 = runStore.claimRun(runId, "worker-uuid-1", 300);
    QVERIFY(claim1.isSuccess());
    auto claim2 = runStore.claimRun(runId, "worker-uuid-2", 300);
    QVERIFY(!claim2.isSuccess());
    QCOMPARE(claim2.error, store::TransitionError::Conflict);

    // Same worker can renew lease; different worker cannot
    QVERIFY(runStore.renewLease(runId, "worker-uuid-1", 600).isSuccess());
    QVERIFY(!runStore.renewLease(runId, "worker-uuid-2", 600).isSuccess());

    // Restart with a new worker-uuid-2 deterministically recovers stale running run to pending
    auto recRes = runStore.recoverStaleRuns("worker-uuid-2");
    QVERIFY(recRes.isSuccess());
    QCOMPARE(*recRes.value, 1);

    auto recovered = runStore.getRun(runId);
    QVERIFY(recovered.has_value());
    QCOMPARE(recovered->state, QStringLiteral("pending"));
    QVERIFY(!recovered->lastError.isEmpty());

    sqlite3_close(rawDb);
}

void TestRunStore::testDescendantRunPreservesOriginalHistory() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(database, &err), qPrintable(err));

    store::PromptStore promptStore(database);
    auto rev = promptStore.createRevision("user", "", "merge", 10, "", "Merge A and B", "llama3", true);
    QVERIFY(rev.isSuccess());

    store::RunStore runStore(database);
    QList<store::AiRunInput> inputs = {{0, 0, 0, 11, 101, "source"}, {0, 0, 1, 12, 102, "source"}};
    auto run1 = runStore.enqueueRun(rev.value->id, "merge", 10, 0, "merge", "llama3", 0, std::nullopt, inputs);
    QVERIFY(run1.isSuccess());
    QVERIFY(runStore.claimRun(run1.value->id, "w1").isSuccess());
    QVERIFY(runStore.failRun(run1.value->id, "Transient failure").isSuccess());

    // Retry creates a descendant run instead of overwriting run1
    auto run2 = runStore.createDescendantRun(run1.value->id, "Merge A and B (refined)", "mistral");
    QVERIFY(run2.isSuccess());
    QCOMPARE(run2.value->parentRunId, std::optional<int>(run1.value->id));
    QCOMPARE(run2.value->state, QStringLiteral("pending"));
    QCOMPARE(run2.value->model, QStringLiteral("mistral"));

    // Run1 remains intact in failed state; run2 inherits ordered inputs
    auto r1Check = runStore.getRun(run1.value->id);
    QCOMPARE(r1Check->state, QStringLiteral("failed"));
    const auto r2Inputs = runStore.getRunInputs(run2.value->id);
    QCOMPARE(r2Inputs.size(), 2);
    QCOMPARE(r2Inputs.at(0).versionId, 101);
    QCOMPARE(r2Inputs.at(1).versionId, 102);

    sqlite3_close(rawDb);
}

void TestRunStore::testOptimisticApplyConflictDetection() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(database, &err), qPrintable(err));

    store::ArtifactStore artStore(database);
    auto created = artStore.createArtifact(store::ArtifactKind::Document, 0, "Doc", "Base Content", "");
    QVERIFY(created.isSuccess());
    const int artId = created.value->artifactId;
    const int baseVerId = created.value->id;

    store::PromptStore promptStore(database);
    auto rev = promptStore.createRevision("user", "", "document", artId, "", "Rewrite", "llama3", true);
    QVERIFY(rev.isSuccess());

    store::RunStore runStore(database);
    auto runRes = runStore.enqueueRun(rev.value->id, "document", artId, baseVerId, "replace", "llama3");
    QVERIFY(runRes.isSuccess());
    QVERIFY(runStore.claimRun(runRes.value->id, "w1").isSuccess());
    QVERIFY(runStore.completeRun(runRes.value->id, "AI Proposed Content").isSuccess());

    // User seals base version and creates a concurrent descendant edit before AI output is applied
    QVERIFY(artStore.sealVersion(baseVerId).isSuccess());
    auto userEdit = artStore.createMutableDescendant(baseVerId, "Doc", "User Concurrent Edit", "");
    QVERIFY(userEdit.isSuccess());

    // Applying stale AI run output must detect conflict and NOT overwrite user edit
    auto applyRes = runStore.applyRunOutput(runRes.value->id, "Doc");
    QVERIFY(!applyRes.isSuccess());
    QCOMPARE(applyRes.error, store::TransitionError::Conflict);

    auto currentVer = artStore.getCurrentVersion(artId);
    QCOMPARE(currentVer->content, QStringLiteral("User Concurrent Edit"));

    auto conflictRun = runStore.getRun(runRes.value->id);
    QCOMPARE(conflictRun->state, QStringLiteral("conflict"));

    sqlite3_close(rawDb);
}

void TestRunStore::testMigration28To29BackfillsQueueHistoryMergesAndQuarantinesNulls() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);

    db::MigrationRunner fullRunner = db::MigrationFactory::createRunner();
    db::MigrationRunner runner28;
    for (const auto& m : fullRunner.getMigrations()) {
        if (m.toVersion <= 28) runner28.addMigration(m);
    }
    QString err;
    QVERIFY2(runner28.run(database, &err), qPrintable(err));

    // Seed legacy prompt_history (1 valid, 1 NULL prompt), queue (1 processing, 1 invalid target_type), document_merges
    QVERIFY(database.execute(
        "INSERT INTO prompt_history (id, document_id, prompt, model) VALUES (1, 10, 'Legacy P', 'm1');"));
    QVERIFY(
        database.execute("INSERT INTO prompt_history (id, document_id, prompt, model) VALUES (2, 10, NULL, 'm1');"));
    QVERIFY(
        database.execute("INSERT INTO queue (id, message_id, model, prompt, processing_id, target_type, state) "
                         "VALUES (100, 10, 'm1', 'Queued P', 99999, 'document', 'processing');"));
    QVERIFY(
        database.execute("INSERT INTO queue (id, message_id, model, prompt, processing_id, target_type, state) "
                         "VALUES (101, 10, 'm1', 'Bad Type', 0, 'bogus_type', 'pending');"));
    QVERIFY(
        database.execute("INSERT INTO document_merges (id, document_id, source_document_ids) VALUES (5, 10, '1,2');"));
    QVERIFY(database.execute(
        "INSERT INTO document_merges (id, document_id, source_document_ids) VALUES (6, 10, 'invalid,csv');"));

    QVERIFY2(fullRunner.run(database, &err), qPrintable(err));

    store::RunStore runStore(database);
    auto migratedRun = runStore.getRun(100);
    QVERIFY(migratedRun.has_value());
    QCOMPARE(migratedRun->state, QStringLiteral("pending"));

    store::RecoveryStore recoveryStore(database);
    QVERIFY(recoveryStore.getRecords(QString(), "prompt_history").size() >= 1);
    QVERIFY(recoveryStore.getRecords(QString(), "queue").size() >= 1);
    QVERIFY(recoveryStore.getRecords(QString(), "document_merges").size() >= 1);

    sqlite3_close(rawDb);
}

void TestRunStore::testApplyRunOutputAtomicRollback() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    QString err;
    QVERIFY2(db::MigrationFactory::createRunner().run(database, &err), qPrintable(err));

    store::ArtifactStore artStore(database);
    auto created = artStore.createArtifact(store::ArtifactKind::Document, 0, "Doc", "Original Sealed", "");
    QVERIFY(created.isSuccess());
    const int artId = created.value->artifactId;
    const int baseVerId = created.value->id;
    QVERIFY(artStore.sealVersion(baseVerId).isSuccess());

    store::PromptStore promptStore(database);
    auto rev = promptStore.createRevision("user", "", "document", artId, "", "Polish", "llama3", true);
    QVERIFY(rev.isSuccess());

    store::RunStore runStore(database);
    auto runRes = runStore.enqueueRun(rev.value->id, "document", artId, baseVerId, "replace", "llama3");
    QVERIFY(runRes.isSuccess());
    QVERIFY(runStore.claimRun(runRes.value->id, "w1").isSuccess());
    QVERIFY(runStore.completeRun(runRes.value->id, "AI Applied Text").isSuccess());

    // Failure injection: abort UPDATE ai_runs when transitioning to 'applied'
    QVERIFY(
        database.execute("CREATE TRIGGER trg_fail_apply_run BEFORE UPDATE ON ai_runs "
                         "WHEN NEW.state = 'applied' BEGIN SELECT RAISE(ABORT, 'injected apply failure'); END;"));

    auto failedApply = runStore.applyRunOutput(runRes.value->id, "Doc");
    QVERIFY(!failedApply.isSuccess());

    // Verify artifact version was rolled back completely (still at Original Sealed, only 1 version)
    auto currentVer = artStore.getCurrentVersion(artId);
    QVERIFY(currentVer.has_value());
    QCOMPARE(currentVer->id, baseVerId);
    QCOMPARE(currentVer->content, QStringLiteral("Original Sealed"));
    auto history = artStore.getVersionHistory(artId);
    QVERIFY(history.isSuccess());
    QCOMPARE(history.value->size(), 1);
    QCOMPARE(runStore.getRun(runRes.value->id)->state, QStringLiteral("completed"));

    // Remove injected trigger and verify atomic apply succeeds across sealed base version
    QVERIFY(database.execute("DROP TRIGGER trg_fail_apply_run;"));
    auto okApply = runStore.applyRunOutput(runRes.value->id, "Doc");
    QVERIFY2(okApply.isSuccess(), qPrintable(okApply.errorMessage));
    QVERIFY(okApply.value->id != baseVerId);
    QCOMPARE(artStore.getCurrentVersion(artId)->content, QStringLiteral("AI Applied Text"));
    QCOMPARE(runStore.getRun(runRes.value->id)->state, QStringLiteral("applied"));
    QCOMPARE(runStore.getRun(runRes.value->id)->resultVersionId, okApply.value->id);

    sqlite3_close(rawDb);
}

void TestRunStore::testDeleteQueueItemPreservesRunHistory() {
    BookDatabase book(":memory:");
    QVERIFY(book.open(""));

    int docId = book.addDocument(0, "D", "C");
    QVERIFY(docId > 0);

    sqlite3* raw = reinterpret_cast<sqlite3*>(book.getDatabaseHandleForTesting());
    db::Database db(raw);
    store::PromptStore promptStore(db);
    auto rev = promptStore.createRevision("user", "", "document", docId, "", "Summarize", "llama3", true);
    QVERIFY(rev.isSuccess());

    store::RunStore runStore(db);
    store::AiRunInput in;
    in.ordinal = 0;
    in.artifactId = docId;
    in.versionId = 0;
    in.role = "source";
    auto pendingRun =
        runStore.enqueueRun(rev.value->id, "document", docId, 0, "replace", "llama3", 0, std::nullopt, {in});
    QVERIFY(pendingRun.isSuccess());
    auto completedRun =
        runStore.enqueueRun(rev.value->id, "document", docId, 0, "replace", "llama3", 0, std::nullopt, {in});
    QVERIFY(completedRun.isSuccess());
    QVERIFY(runStore.claimRun(completedRun.value->id, "w1").isSuccess());
    QVERIFY(runStore.completeRun(completedRun.value->id, "Retained Output").isSuccess());

    // deleteQueueItem must cancel pending/running and archive completed without deleting ai_runs or ai_run_inputs
    QVERIFY(book.deleteQueueItem(pendingRun.value->id));
    QVERIFY(book.deleteQueueItem(completedRun.value->id));

    QVERIFY(book.getQueue().isEmpty());

    auto pAfter = runStore.getRun(pendingRun.value->id);
    QVERIFY(pAfter.has_value());
    QCOMPARE(pAfter->state, QStringLiteral("cancelled"));
    QCOMPARE(runStore.getRunInputs(pendingRun.value->id).size(), 1);

    auto cAfter = runStore.getRun(completedRun.value->id);
    QVERIFY(cAfter.has_value());
    QCOMPARE(cAfter->state, QStringLiteral("archived"));
    QCOMPARE(cAfter->responseText, QStringLiteral("Retained Output"));
    QCOMPARE(runStore.getRunInputs(completedRun.value->id).size(), 1);
}

QTEST_MAIN(TestRunStore)
#include "test_RunStore.moc"
