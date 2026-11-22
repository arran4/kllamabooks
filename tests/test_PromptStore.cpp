#include <sqlcipher/sqlite3.h>

#include <QtTest>

#include "../src/db/Database.h"
#include "../src/db/MigrationFactory.h"
#include "../src/store/PromptStore.h"

class TestPromptStore : public QObject {
    Q_OBJECT

   private slots:
    void testCreateSealAndImmutabilityTrigger();
    void testQueryRevisionsByTarget();
};

void TestPromptStore::testCreateSealAndImmutabilityTrigger() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(database, &err), qPrintable(err));

    store::PromptStore promptStore(database);

    // Unsealed revision can be sealed later
    auto revRes = promptStore.createRevision("user", "tpl:1", "document", 42, "System", "Prompt v1", "llama3", false);
    QVERIFY(revRes.isSuccess());
    QVERIFY(!revRes.value->isSealed);

    auto sealRes = promptStore.sealRevision(revRes.value->id);
    QVERIFY(sealRes.isSuccess());

    auto reloaded = promptStore.getRevision(revRes.value->id);
    QVERIFY(reloaded.has_value());
    QVERIFY(reloaded->isSealed);

    // Mutating a sealed prompt revision must be rejected by SQLite trigger
    QString triggerErr;
    QVERIFY(!database.execute(
        QString("UPDATE prompt_revisions SET prompt_text = 'tampered' WHERE id = %1;").arg(revRes.value->id),
        &triggerErr));
    QVERIFY(triggerErr.contains("Cannot modify sealed prompt_revision"));

    sqlite3_close(rawDb);
}

void TestPromptStore::testQueryRevisionsByTarget() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(database, &err), qPrintable(err));

    store::PromptStore promptStore(database);
    QVERIFY(promptStore.createRevision("user", "", "document", 7, "", "P1", "m1", true).isSuccess());
    QVERIFY(promptStore.createRevision("template", "tpl:2", "document", 7, "Sys", "P2", "m2", true).isSuccess());
    QVERIFY(promptStore.createRevision("user", "", "message", 7, "", "Chat P", "m1", true).isSuccess());

    const auto docRevs = promptStore.getRevisionsForTarget("document", 7);
    QCOMPARE(docRevs.size(), 2);
    QCOMPARE(docRevs.at(0).promptText, QStringLiteral("P1"));
    QCOMPARE(docRevs.at(1).promptText, QStringLiteral("P2"));

    sqlite3_close(rawDb);
}

QTEST_MAIN(TestPromptStore)
#include "test_PromptStore.moc"
