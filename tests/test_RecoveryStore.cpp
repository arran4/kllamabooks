#include <sqlcipher/sqlite3.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "../src/BookDatabase.h"
#include "../src/db/Database.h"
#include "../src/db/MigrationFactory.h"
#include "../src/db/Migrations.h"
#include "../src/store/ArtifactStore.h"
#include "../src/store/RecoveryStore.h"

class TestRecoveryStore : public QObject {
    Q_OBJECT

   private slots:
    void init();
    void cleanup();

    void testAddAndQueryQuarantinedRecords();
    void testValidationAndStatusConstraints();
    void testExportRecordsToJson();
    void testReimportAsArtifact();
    void testReassignRecord();
    void testDeleteRecordPermanently();
    void testEncryptedBackupAndRestorePreMigrationRollback();
    void testBackupAndRestoreRejectsPlaintextOrCorruptFiles();
    void testOpenRefusesMigrationWhenBackupFails();
    void testWalCheckpointAndBusyBackupRefusal();
    void testRestoreRollbackOnUpgradeFailure();
    void testRestoreFaultsBeforeStagingPartialMoveAndPermissions();
    void testFailedRestoreDoesNotTriggerForwardMigration();
    void testCrashRecoveryJournalAndRetry();
    void testExistingRecoveryArtifactsNeverDestroyedAndAmbiguousStateFailsSafely();
    void testDurabilitySyncAndCleanupErrorPropagation();
    void testRestoreJournalStrictValidationAndSymlinkRejection();
    void testBackupSnapshotConsistencyUnderCompetingWrites();

   private:
    sqlite3* m_handle = nullptr;
    db::Database* m_db = nullptr;
};

namespace {
void verifyOwnerOnlyPermissions(const QString& path) {
    QFileInfo info(path);
    QVERIFY2(info.exists(), qPrintable(path));
    const QFileDevice::Permissions perms = info.permissions();
    QVERIFY((perms & (QFileDevice::ReadOwner | QFileDevice::WriteOwner)) ==
            (QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                       QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)));
}
}  // namespace

void TestRecoveryStore::init() {
    QCOMPARE(sqlite3_open(":memory:", &m_handle), SQLITE_OK);
    m_db = new db::Database(m_handle);
    QVERIFY(m_db->execute("PRAGMA foreign_keys = ON;"));

    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString error;
    QVERIFY2(runner.run(*m_db, &error), qPrintable(error));
}

void TestRecoveryStore::cleanup() {
    BookDatabase::setRestoreFaultHookForTesting(nullptr);
    delete m_db;
    m_db = nullptr;
    if (m_handle) {
        sqlite3_close(m_handle);
        m_handle = nullptr;
    }
}

void TestRecoveryStore::testAddAndQueryQuarantinedRecords() {
    store::RecoveryStore recovery(*m_db);
    QString error;

    auto id1 =
        recovery.addQuarantinedRecord("document_history", 42, "document", 999, "Orphan History", "Historical body",
                                      "{\"id\":42,\"document_id\":999}", "missing_parent_document", &error);
    QVERIFY2(id1.has_value(), qPrintable(error));

    auto id2 =
        recovery.addQuarantinedRecord("drafts", 7, "unknown_kind", 12, QString(), "Draft body",
                                      "{\"id\":7,\"target_type\":\"unknown_kind\"}", "unknown_target_type", &error);
    QVERIFY2(id2.has_value(), qPrintable(error));

    QCOMPARE(recovery.getQuarantinedCount(&error), 2);

    auto rec1 = recovery.getRecord(*id1, &error);
    QVERIFY(rec1.has_value());
    QCOMPARE(rec1->sourceTable, QString("document_history"));
    QVERIFY(rec1->legacyId.has_value());
    QCOMPARE(*rec1->legacyId, 42);
    QCOMPARE(rec1->targetType, QString("document"));
    QVERIFY(rec1->parentReference.has_value());
    QCOMPARE(*rec1->parentReference, 999);
    QCOMPARE(rec1->title, QString("Orphan History"));
    QCOMPARE(rec1->content, QString("Historical body"));
    QCOMPARE(rec1->reason, QString("missing_parent_document"));
    QCOMPARE(rec1->status, QString("quarantined"));

    auto byTable = recovery.getRecords("quarantined", "drafts", &error);
    QCOMPARE(byTable.size(), 1);
    QCOMPARE(byTable.first().id, *id2);
}

void TestRecoveryStore::testValidationAndStatusConstraints() {
    store::RecoveryStore recovery(*m_db);
    QString error;

    QVERIFY(!recovery.addQuarantinedRecord("", 1, "document", 1, "T", "C", "{}", "reason", &error).has_value());
    QVERIFY(!recovery.addQuarantinedRecord("drafts", 1, "document", 1, "T", "C", "", "reason", &error).has_value());
    QVERIFY(!recovery.addQuarantinedRecord("drafts", 1, "document", 1, "T", "C", "{}", "", &error).has_value());

    // Direct SQL insertion with invalid status must fail CHECK constraint
    QVERIFY(
        !m_db->execute("INSERT INTO quarantined_records (source_table, raw_payload_json, reason, status) "
                       "VALUES ('drafts', '{}', 'bad', 'invalid_status');",
                       &error));
}

void TestRecoveryStore::testExportRecordsToJson() {
    store::RecoveryStore recovery(*m_db);
    QString error;

    auto id1 = recovery.addQuarantinedRecord("document_history", 10, "document", 500, "Export Title", "Export Body",
                                             "{\"id\":10,\"action_type\":\"manual_edit_pre\"}",
                                             "missing_parent_document", &error);
    QVERIFY(id1.has_value());

    QByteArray jsonBytes = recovery.exportRecordsToJson({*id1}, &error);
    QVERIFY2(!jsonBytes.isEmpty(), qPrintable(error));

    QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
    QVERIFY(doc.isObject());
    QJsonArray arr = doc.object()["quarantined_records"].toArray();
    QCOMPARE(arr.size(), 1);
    QJsonObject item = arr.first().toObject();
    QCOMPARE(item["id"].toInt(), *id1);
    QCOMPARE(item["source_table"].toString(), QString("document_history"));
    QCOMPARE(item["raw_payload"].toObject()["action_type"].toString(), QString("manual_edit_pre"));

    // Exporting non-existent ID fails cleanly
    QByteArray missingJson = recovery.exportRecordsToJson({99999}, &error);
    QVERIFY(missingJson.isEmpty());
}

void TestRecoveryStore::testReimportAsArtifact() {
    store::RecoveryStore recovery(*m_db);
    store::ArtifactStore artifacts(*m_db);
    QString error;

    auto recId =
        recovery.addQuarantinedRecord("document_history", 5, "document", 77, "Recovered Chapter",
                                      "Recovered chapter text", "{\"id\":5}", "missing_parent_document", &error);
    QVERIFY(recId.has_value());

    auto newArtId = recovery.reimportAsArtifact(*recId, "document", 0, &error);
    QVERIFY2(newArtId.has_value(), qPrintable(error));

    auto art = artifacts.getArtifact(*newArtId);
    QVERIFY(art.has_value());
    QCOMPARE(art->kind, store::ArtifactKind::Document);

    auto ver = artifacts.getCurrentVersion(*newArtId);
    QVERIFY(ver.has_value());
    QCOMPARE(ver->title, QString("Recovered Chapter"));
    QCOMPARE(ver->content, QString("Recovered chapter text"));

    auto updatedRec = recovery.getRecord(*recId, &error);
    QVERIFY(updatedRec.has_value());
    QCOMPARE(updatedRec->status, QString("reimported"));
    QCOMPARE(updatedRec->resolvedTargetKind, QString("document"));
    QVERIFY(updatedRec->resolvedTargetId.has_value());
    QCOMPARE(*updatedRec->resolvedTargetId, *newArtId);
    QVERIFY(!updatedRec->resolvedAt.isEmpty());
    QCOMPARE(recovery.getQuarantinedCount(&error), 0);

    // Re-importing an already resolved record must be rejected
    QVERIFY(!recovery.reimportAsArtifact(*recId, "document", 0, &error).has_value());
}

void TestRecoveryStore::testReassignRecord() {
    store::RecoveryStore recovery(*m_db);
    store::ArtifactStore artifacts(*m_db);
    QString error;

    auto createRes = artifacts.createArtifact(store::ArtifactKind::Document, 0, "Target Doc", "Body", "");
    QVERIFY(createRes.isSuccess());
    int targetArtId = createRes.value->artifactId;

    auto recId = recovery.addQuarantinedRecord("comments", 8, "document", 999, QString(), "Orphan comment",
                                               "{\"id\":8}", "missing_target_entity", &error);
    QVERIFY(recId.has_value());

    // Reassigning to non-existent artifact fails
    QVERIFY(!recovery.reassignRecord(*recId, "document", 99999, &error));

    // Reassigning to existing artifact succeeds
    QVERIFY2(recovery.reassignRecord(*recId, "document", targetArtId, &error), qPrintable(error));

    auto updated = recovery.getRecord(*recId, &error);
    QVERIFY(updated.has_value());
    QCOMPARE(updated->status, QString("reassigned"));
    QCOMPARE(updated->resolvedTargetKind, QString("document"));
    QCOMPARE(*updated->resolvedTargetId, targetArtId);
}

void TestRecoveryStore::testDeleteRecordPermanently() {
    store::RecoveryStore recovery(*m_db);
    QString error;

    auto recId = recovery.addQuarantinedRecord("drafts", 3, "document", 404, "Stale", "Body", "{\"id\":3}",
                                               "missing_parent", &error);
    QVERIFY(recId.has_value());

    QVERIFY2(recovery.deleteRecordPermanently(*recId, &error), qPrintable(error));
    QVERIFY(!recovery.getRecord(*recId, &error).has_value());
    QVERIFY(!recovery.deleteRecordPermanently(*recId, &error));
}

void TestRecoveryStore::testEncryptedBackupAndRestorePreMigrationRollback() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("book_live.db");
    const QString backupPath = tempDir.filePath("book_v25_backup.db");
    const QString password = "strong_book_password_123";

    // 1. Create a v25 encrypted database on disk
    sqlite3* rawHandle = nullptr;
    QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &rawHandle), SQLITE_OK);
    db::Database rawDb(rawHandle);
    QVERIFY(rawDb.execute(QString("PRAGMA key = '%1';").arg(password)));
    db::MigrationRunner fullRunner = db::MigrationFactory::createRunner();
    db::MigrationRunner runner25;
    for (const auto& m : fullRunner.getMigrations()) {
        if (m.toVersion <= 25) {
            runner25.addMigration(m);
        }
    }
    QString error;
    QVERIFY2(runner25.run(rawDb, &error), qPrintable(error));
    QVERIFY(
        rawDb.execute("INSERT INTO documents (id, folder_id, title, content) VALUES (1, 0, 'Pre-Upgrade Doc', "
                      "'Original V25 Text');"));
    sqlite3_close(rawHandle);

    // 2. Open with BookDatabase (auto-creates .pre_v<target>.bak before upgrading), then mutate live DB
    BookDatabase book(dbPath);
    QVERIFY(book.open(password));

    const int targetVersion = fullRunner.getMigrations().back().toVersion;
    const QString autoBackupPath = QString("%1.pre_v%2.bak").arg(dbPath).arg(targetVersion);
    verifyOwnerOnlyPermissions(autoBackupPath);

    QVERIFY2(book.createEncryptedBackup(backupPath, &error), qPrintable(error));
    verifyOwnerOnlyPermissions(backupPath);

    QFile rawBackup(backupPath);
    QVERIFY(rawBackup.open(QIODevice::ReadOnly));
    const QByteArray backupBytes = rawBackup.readAll();
    rawBackup.close();
    QVERIFY(!backupBytes.startsWith("SQLite format 3"));
    QVERIFY(!backupBytes.contains("Original V25 Text"));

    // Mutate live database after backup
    book.updateDocument(1, "Post-Backup Mutated Title", "Mutated Content");
    auto mutatedDoc = book.getDocument(1);
    QVERIFY(mutatedDoc.has_value());
    QCOMPARE(mutatedDoc->title, QString("Post-Backup Mutated Title"));

    // 3. Restore from post-migration backup (upgradeSchema=false) and verify mutated state rolled back
    QVERIFY2(book.restoreEncryptedBackup(backupPath, password, false, &error), qPrintable(error));
    QVERIFY(book.isOpen());
    verifyOwnerOnlyPermissions(dbPath);

    auto restoredDoc = book.getDocument(1);
    QVERIFY(restoredDoc.has_value());
    QCOMPARE(restoredDoc->title, QString("Pre-Upgrade Doc"));
    QCOMPARE(restoredDoc->content, QString("Original V25 Text"));

    // 4. Restore from auto-created pre-migration backup with upgradeSchema=false -> stays at v25
    QVERIFY2(book.restoreEncryptedBackup(autoBackupPath, password, false, &error), qPrintable(error));
    QVERIFY(book.isOpen());
    verifyOwnerOnlyPermissions(dbPath);
    {
        db::Database restoredV25Db(reinterpret_cast<sqlite3*>(book.getDatabaseHandleForTesting()));
        int ver = 0;
        QVERIFY(restoredV25Db.queryInt("PRAGMA user_version;", ver));
        QCOMPARE(ver, 25);

        sqlite3_stmt* stmt = nullptr;
        QCOMPARE(sqlite3_prepare_v2(restoredV25Db.handle(), "SELECT title, content FROM documents WHERE id = 1;", -1,
                                    &stmt, nullptr),
                 SQLITE_OK);
        QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))),
                 QString("Pre-Upgrade Doc"));
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))),
                 QString("Original V25 Text"));
        sqlite3_finalize(stmt);
    }

    // 5. Restore from auto-created pre-migration backup with upgradeSchema=true -> upgrades to targetVersion
    QVERIFY2(book.restoreEncryptedBackup(autoBackupPath, password, true, &error), qPrintable(error));
    QVERIFY(book.isOpen());
    verifyOwnerOnlyPermissions(dbPath);
    {
        db::Database upgradedRestoredDb(reinterpret_cast<sqlite3*>(book.getDatabaseHandleForTesting()));
        int ver = 0;
        QVERIFY(upgradedRestoredDb.queryInt("PRAGMA user_version;", ver));
        QCOMPARE(ver, targetVersion);
    }
    auto upgradedDoc = book.getDocument(1);
    QVERIFY(upgradedDoc.has_value());
    QCOMPARE(upgradedDoc->title, QString("Pre-Upgrade Doc"));
    QCOMPARE(upgradedDoc->content, QString("Original V25 Text"));
    book.close();
}

void TestRecoveryStore::testBackupAndRestoreRejectsPlaintextOrCorruptFiles() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("encrypted_book.db");
    const QString plaintextPath = tempDir.filePath("plaintext.db");
    const QString corruptPath = tempDir.filePath("corrupt.db");
    const QString password = "secret_password";

    BookDatabase book(dbPath);
    QVERIFY(book.open(password));
    int docId = book.addDocument(0, "Safe Doc", "Safe Content");
    QVERIFY(docId > 0);

    // Create an unencrypted SQLite file at plaintextPath
    sqlite3* plainHandle = nullptr;
    QCOMPARE(sqlite3_open(plaintextPath.toUtf8().constData(), &plainHandle), SQLITE_OK);
    QCOMPARE(sqlite3_exec(plainHandle, "CREATE TABLE t (id INT);", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(plainHandle);

    // Create a corrupt random file at corruptPath
    QFile corruptFile(corruptPath);
    QVERIFY(corruptFile.open(QIODevice::WriteOnly));
    corruptFile.write("not a valid sqlcipher database payload at all");
    corruptFile.close();

    QString error;
    // Rejects plaintext file when restoring an encrypted book
    QVERIFY(!book.restoreEncryptedBackup(plaintextPath, password, false, &error));
    // Rejects wrong password
    QVERIFY(!book.createEncryptedBackup(tempDir.path(), &error));
    // Rejects corrupt file
    QVERIFY(!book.restoreEncryptedBackup(corruptPath, password, false, &error));

    // Verify original book remains open and intact after failed restores
    QVERIFY(book.isOpen());
    auto doc = book.getDocument(docId);
    QVERIFY(doc.has_value());
    QCOMPARE(doc->title, QString("Safe Doc"));

    // Create valid backup, then verify wrong-password restore rejects and keeps original intact
    const QString validBackup = tempDir.filePath("valid.bak");
    QVERIFY2(book.createEncryptedBackup(validBackup, &error), qPrintable(error));
    QVERIFY(!book.restoreEncryptedBackup(validBackup, "wrong_password_here", false, &error));
    QVERIFY(book.isOpen());
    auto docAfterWrongPw = book.getDocument(docId);
    QVERIFY(docAfterWrongPw.has_value());
    QCOMPARE(docAfterWrongPw->title, QString("Safe Doc"));
    book.close();
}

void TestRecoveryStore::testOpenRefusesMigrationWhenBackupFails() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("blocked_migration.db");
    const QString password = "test_password_pr2";

    // Create a v25 encrypted database
    sqlite3* rawHandle = nullptr;
    QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &rawHandle), SQLITE_OK);
    db::Database rawDb(rawHandle);
    QVERIFY(rawDb.execute(QString("PRAGMA key = '%1';").arg(password)));
    db::MigrationRunner fullRunner = db::MigrationFactory::createRunner();
    db::MigrationRunner runner25;
    for (const auto& m : fullRunner.getMigrations()) {
        if (m.toVersion <= 25) runner25.addMigration(m);
    }
    QString error;
    QVERIFY2(runner25.run(rawDb, &error), qPrintable(error));
    sqlite3_close(rawHandle);

    // Block the pre-migration backup path by placing a directory there
    const int targetVersion = fullRunner.getMigrations().back().toVersion;
    QVERIFY(QDir().mkpath(QString("%1.pre_v%2.bak").arg(dbPath).arg(targetVersion)));

    // BookDatabase::open() must refuse to migrate when encrypted backup fails
    BookDatabase book(dbPath);
    QVERIFY(!book.open(password));
    QVERIFY(!book.isOpen());

    // Confirm database on disk remained at v25
    sqlite3* verifyHandle = nullptr;
    QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &verifyHandle), SQLITE_OK);
    db::Database verifyDb(verifyHandle);
    QVERIFY(verifyDb.execute(QString("PRAGMA key = '%1';").arg(password)));
    int ver = 0;
    QVERIFY(verifyDb.queryInt("PRAGMA user_version;", ver));
    QCOMPARE(ver, 25);
    sqlite3_close(verifyHandle);
}

void TestRecoveryStore::testWalCheckpointAndBusyBackupRefusal() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("wal_book.db");
    const QString backupPath = tempDir.filePath("wal_backup.db");
    const QString standaloneCopyPath = tempDir.filePath("wal_standalone.db");
    const QString password = "wal_backup_password_456";

    BookDatabase book(dbPath);
    QVERIFY(book.open(password));

    db::Database primaryDb(reinterpret_cast<sqlite3*>(book.getDatabaseHandleForTesting()));
    QString error;
    QVERIFY2(primaryDb.execute("PRAGMA journal_mode = WAL;", &error), qPrintable(error));

    const int doc1 = book.addDocument(0, "WAL Flush Doc", "Written under WAL mode");
    QVERIFY(doc1 > 0);

    // 1. WAL checkpoint succeeds -> standalone backup file contains flushed WAL data without -wal sidecar
    QVERIFY2(book.createEncryptedBackup(backupPath, &error), qPrintable(error));
    verifyOwnerOnlyPermissions(backupPath);
    QVERIFY(QFile::copy(backupPath, standaloneCopyPath));

    BookDatabase standaloneBook(standaloneCopyPath);
    QVERIFY(standaloneBook.open(password));
    auto recoveredDoc1 = standaloneBook.getDocument(doc1);
    QVERIFY(recoveredDoc1.has_value());
    QCOMPARE(recoveredDoc1->title, QString("WAL Flush Doc"));
    QCOMPARE(recoveredDoc1->content, QString("Written under WAL mode"));
    standaloneBook.close();

    // 2. Open a second reader holding an older WAL snapshot while primary writes new WAL frames
    sqlite3* readerHandle = nullptr;
    QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &readerHandle), SQLITE_OK);
    db::Database readerDb(readerHandle);
    QVERIFY2(readerDb.execute(QString("PRAGMA key = '%1';").arg(password), &error), qPrintable(error));
    QVERIFY2(readerDb.execute("BEGIN;", &error), qPrintable(error));
    int countBefore = 0;
    QVERIFY2(readerDb.queryInt("SELECT COUNT(*) FROM sqlite_master;", countBefore, &error), qPrintable(error));
    QVERIFY(countBefore > 0);

    const int doc2 = book.addDocument(0, "Blocked WAL Doc", "Should wait for checkpoint");
    QVERIFY(doc2 > 0);

    // createEncryptedBackup must refuse when WAL checkpoint cannot truncate due to active reader snapshot,
    // and must preserve the pre-existing valid backup file intact (overwrite safety).
    QString walError;
    QVERIFY(!book.createEncryptedBackup(backupPath, &walError));
    QVERIFY(walError.contains("WAL", Qt::CaseInsensitive));
    QVERIFY(QFile::exists(backupPath));

    // Confirm existing backup still contains only doc1 (was not overwritten/corrupted)
    BookDatabase preservedBackupCheck(tempDir.filePath("preserved_check.db"));
    QVERIFY(QFile::copy(backupPath, tempDir.filePath("preserved_check.db")));
    QVERIFY(preservedBackupCheck.open(password));
    QVERIFY(preservedBackupCheck.getDocument(doc1).has_value());
    QVERIFY(!preservedBackupCheck.getDocument(doc2).has_value());
    preservedBackupCheck.close();

    // Release reader snapshot and verify backup now succeeds cleanly
    QVERIFY2(readerDb.execute("COMMIT;", &error), qPrintable(error));
    sqlite3_close(readerHandle);

    QVERIFY2(book.createEncryptedBackup(backupPath, &error), qPrintable(error));
    verifyOwnerOnlyPermissions(backupPath);
    book.close();
}

void TestRecoveryStore::testRestoreRollbackOnUpgradeFailure() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("live_book.db");
    const QString v25BackupPath = tempDir.filePath("v25_source.bak");
    const QString password = "restore_rollback_pw_789";

    // 1. Create a separate valid v25 encrypted backup
    db::MigrationRunner fullRunner = db::MigrationFactory::createRunner();
    const int targetVersion = fullRunner.getMigrations().back().toVersion;
    {
        sqlite3* v25Handle = nullptr;
        QCOMPARE(sqlite3_open(v25BackupPath.toUtf8().constData(), &v25Handle), SQLITE_OK);
        db::Database v25Db(v25Handle);
        QVERIFY(v25Db.execute(QString("PRAGMA key = '%1';").arg(password)));
        db::MigrationRunner runner25;
        for (const auto& m : fullRunner.getMigrations()) {
            if (m.toVersion <= 25) runner25.addMigration(m);
        }
        QString error;
        QVERIFY2(runner25.run(v25Db, &error), qPrintable(error));
        QVERIFY(v25Db.execute(
            "INSERT INTO documents (id, folder_id, title, content) VALUES (1, 0, 'Old V25 Doc', 'Old V25 Body');"));
        sqlite3_close(v25Handle);
    }

    // 2. Open live DB at targetVersion with live content
    BookDatabase liveBook(dbPath);
    QVERIFY(liveBook.open(password));
    const int liveDocId = liveBook.addDocument(0, "Live Current Doc", "Must survive failed restore upgrade");
    QVERIFY(liveDocId > 0);

    // Block auto pre-migration backup path that restoreEncryptedBackup(..., upgradeSchema=true) will attempt
    QVERIFY(QDir().mkpath(QString("%1.pre_v%2.bak").arg(dbPath).arg(targetVersion)));

    QString error;
    QVERIFY(!liveBook.restoreEncryptedBackup(v25BackupPath, password, true, &error));
    QVERIFY(!error.isEmpty());

    // Live book must be rolled back, reopened, owner-only, and free of stray temp/rollback artifacts
    QVERIFY(liveBook.isOpen());
    verifyOwnerOnlyPermissions(dbPath);
    QVERIFY(!QFile::exists(dbPath + ".restore.tmp"));
    QVERIFY(!QFile::exists(dbPath + ".pre_restore.bak"));
    auto survivingDoc = liveBook.getDocument(liveDocId);
    QVERIFY(survivingDoc.has_value());
    QCOMPARE(survivingDoc->title, QString("Live Current Doc"));
    QCOMPARE(survivingDoc->content, QString("Must survive failed restore upgrade"));
    liveBook.close();
}

void TestRecoveryStore::testRestoreFaultsBeforeStagingPartialMoveAndPermissions() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("fault_book.db");
    const QString backupPath = tempDir.filePath("valid_backup.db");
    const QString password = "fault_test_password_111";

    // Create initial live DB and a valid older backup
    {
        BookDatabase live(dbPath);
        QVERIFY(live.open(password));
        QVERIFY(live.addDocument(0, "Backup Epoch Doc", "From backup") > 0);
        QString err;
        QVERIFY2(live.createEncryptedBackup(backupPath, &err), qPrintable(err));
        QVERIFY(live.addDocument(0, "Committed After Backup", "Must stay intact on failed restore") > 0);
        live.close();
    }

    auto verifyLiveDbIntactAndClean = [&](const QString& path, bool checkWalCommitted) {
        verifyOwnerOnlyPermissions(path);
        QVERIFY(!QFile::exists(path + ".restore_journal.json"));
        const QDir dir(QFileInfo(path).absolutePath());
        const QString base = QFileInfo(path).fileName();
        QVERIFY(dir.entryList({base + ".restore.*"}, QDir::Files | QDir::Hidden).isEmpty());

        BookDatabase check(path);
        QVERIFY(check.open(password));
        QCOMPARE(check.getDocuments().size(), checkWalCommitted ? 3 : 2);
        check.close();
    };

    // 1. Failure before staging and during staging while DB is open -> keeps DB open & intact
    for (const QString& stage : {QStringLiteral("before_stage_backup"), QStringLiteral("stage_backup")}) {
        BookDatabase live(dbPath);
        QVERIFY(live.open(password));
        BookDatabase::setRestoreFaultHookForTesting([&](const QString& s) { return s == stage; });
        QString err;
        QVERIFY(!live.restoreEncryptedBackup(backupPath, password, false, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(live.isOpen());
        QCOMPARE(live.getDocuments().size(), 2);
        live.close();
        BookDatabase::setRestoreFaultHookForTesting(nullptr);
        verifyLiveDbIntactAndClean(dbPath, false);
    }

    // 2. Failure during placing_replacement and set_restored_permissions -> rolls back completely
    for (const QString& stage : {QStringLiteral("place_replacement"), QStringLiteral("set_restored_permissions")}) {
        BookDatabase live(dbPath);
        QVERIFY(live.open(password));
        BookDatabase::setRestoreFaultHookForTesting([&](const QString& s) { return s == stage; });
        QString err;
        QVERIFY(!live.restoreEncryptedBackup(backupPath, password, false, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(live.isOpen());
        QCOMPARE(live.getDocuments().size(), 2);
        live.close();
        BookDatabase::setRestoreFaultHookForTesting(nullptr);
        verifyLiveDbIntactAndClean(dbPath, false);
    }

    // 3. Partial DB+WAL+SHM move failure (DB moves to .orig, then move_original_wal fails)
    {
        // Enable WAL and create an uncheckpointed -wal / -shm on disk while closed
        BookDatabase walSetup(dbPath);
        QVERIFY(walSetup.open(password));
        db::Database raw(reinterpret_cast<sqlite3*>(walSetup.getDatabaseHandleForTesting()));
        QVERIFY(raw.execute("PRAGMA journal_mode = WAL;"));
        walSetup.close();

        // Write a new doc while holding a second reader so SQLite keeps -wal/-shm on disk after walWriter closes
        sqlite3* reader = nullptr;
        QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &reader), SQLITE_OK);
        db::Database rdb(reader);
        QVERIFY(rdb.execute(QString("PRAGMA key = '%1';").arg(password)));
        QVERIFY(rdb.execute("BEGIN;"));
        int cnt = 0;
        QVERIFY(rdb.queryInt("SELECT COUNT(*) FROM sqlite_master;", cnt));

        BookDatabase walWriter(dbPath);
        QVERIFY(walWriter.open(password));
        QVERIFY(walWriter.addDocument(0, "WAL Uncheckpointed Doc", "In -wal sidecar") > 0);
        walWriter.close();
        const QString partialPath = tempDir.filePath("wal_partial.db");
        QVERIFY(QFile::exists(dbPath + "-wal"));
        QVERIFY(QFile::exists(dbPath + "-shm"));
        QVERIFY(QFile::copy(dbPath, partialPath));
        QVERIFY(QFile::copy(dbPath + "-wal", partialPath + "-wal"));
        QVERIFY(QFile::copy(dbPath + "-shm", partialPath + "-shm"));
        QVERIFY(rdb.execute("COMMIT;"));
        sqlite3_close(reader);

        // Cold restore on partialPath where DB moves to .orig and move_original_wal fails
        BookDatabase partialBook(partialPath);
        BookDatabase::setRestoreFaultHookForTesting([](const QString& s) { return s == "move_original_wal"; });
        QString err;
        QVERIFY(!partialBook.restoreEncryptedBackup(backupPath, password, false, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!partialBook.isOpen());
        BookDatabase::setRestoreFaultHookForTesting(nullptr);

        // Both DB and WAL/SHM were restored cleanly back to partialPath
        QVERIFY(QFile::exists(partialPath));
        QVERIFY(QFile::exists(partialPath + "-wal"));
        QVERIFY(!QFile::exists(partialPath + ".restore_journal.json"));
        BookDatabase verifyPartial(partialPath);
        QVERIFY(verifyPartial.open(password));
        QCOMPARE(verifyPartial.getDocuments().size(), 3);
        verifyPartial.close();
    }
}

void TestRecoveryStore::testFailedRestoreDoesNotTriggerForwardMigration() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("v25_book.db");
    const QString v25BackupA = tempDir.filePath("v25_a.bak");
    const QString v25BackupB = tempDir.filePath("v25_b.bak");
    const QString password = "no_forward_mig_pw_222";

    db::MigrationRunner fullRunner = db::MigrationFactory::createRunner();
    const int targetVersion = fullRunner.getMigrations().back().toVersion;

    auto createV25File = [&](const QString& path, const QString& title) {
        sqlite3* h = nullptr;
        QCOMPARE(sqlite3_open(path.toUtf8().constData(), &h), SQLITE_OK);
        db::Database d(h);
        QVERIFY(d.execute(QString("PRAGMA key = '%1';").arg(password)));
        db::MigrationRunner r25;
        for (const auto& m : fullRunner.getMigrations()) {
            if (m.toVersion <= 25) r25.addMigration(m);
        }
        QString err;
        QVERIFY2(r25.run(d, &err), qPrintable(err));
        QVERIFY(
            d.execute(QString("INSERT INTO documents (id, folder_id, title, content) VALUES (1, 0, '%1', 'Body %1');")
                          .arg(title)));
        sqlite3_close(h);
    };

    createV25File(v25BackupA, "V25 Alpha");
    createV25File(v25BackupB, "V25 Beta");

    // 1. Restore v25BackupA with upgradeSchema = false -> book stays open at v25
    BookDatabase book(dbPath);
    QString err;
    QVERIFY2(book.restoreEncryptedBackup(v25BackupA, password, false, &err), qPrintable(err));
    QVERIFY(book.isOpen());
    {
        db::Database db25(reinterpret_cast<sqlite3*>(book.getDatabaseHandleForTesting()));
        int ver = 0;
        QVERIFY(db25.queryInt("PRAGMA user_version;", ver));
        QCOMPARE(ver, 25);
    }

    // 2. Attempt another restore (both non-upgrading and upgrading) that fails during place_replacement
    BookDatabase::setRestoreFaultHookForTesting([](const QString& s) { return s == "place_replacement"; });
    QVERIFY(!book.restoreEncryptedBackup(v25BackupB, password, false, &err));
    QVERIFY(book.isOpen());
    {
        db::Database still25(reinterpret_cast<sqlite3*>(book.getDatabaseHandleForTesting()));
        int ver = 0;
        QVERIFY(still25.queryInt("PRAGMA user_version;", ver));
        QCOMPARE(ver, 25);
        QVERIFY(!QFile::exists(QString("%1.pre_v%2.bak").arg(dbPath).arg(targetVersion)));
        sqlite3_stmt* stmt = nullptr;
        QCOMPARE(sqlite3_prepare_v2(still25.handle(), "SELECT title FROM documents WHERE id = 1;", -1, &stmt, nullptr),
                 SQLITE_OK);
        QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
        QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))), QString("V25 Alpha"));
        sqlite3_finalize(stmt);
    }

    QVERIFY(!book.restoreEncryptedBackup(v25BackupB, password, true, &err));
    QVERIFY(book.isOpen());
    {
        db::Database still25(reinterpret_cast<sqlite3*>(book.getDatabaseHandleForTesting()));
        int ver = 0;
        QVERIFY(still25.queryInt("PRAGMA user_version;", ver));
        QCOMPARE(ver, 25);
    }
    BookDatabase::setRestoreFaultHookForTesting(nullptr);
    book.close();
}

void TestRecoveryStore::testCrashRecoveryJournalAndRetry() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString backupPath = tempDir.filePath("backup.db");
    const QString password = "crash_journal_pw_333";

    // Create a shared valid backup
    {
        const QString seedPath = tempDir.filePath("seed.db");
        BookDatabase seed(seedPath);
        QVERIFY(seed.open(password));
        QVERIFY(seed.addDocument(0, "Backup Seed", "Backup payload") > 0);
        QString err;
        QVERIFY2(seed.createEncryptedBackup(backupPath, &err), qPrintable(err));
        seed.close();
    }

    // Exercise interrupted crashes at all 3 critical phases + open recovery and retry
    const QStringList crashStages = {QStringLiteral("crash_after_move_original_db"),
                                     QStringLiteral("crash_before_place_replacement"),
                                     QStringLiteral("crash_during_verifying")};

    for (const QString& stage : crashStages) {
        const QString dbPath = tempDir.filePath(QString("book_%1.db").arg(stage));
        {
            BookDatabase initBook(dbPath);
            QVERIFY(initBook.open(password));
            QVERIFY(initBook.addDocument(0, "Surviving Original Title", "Original committed text") > 0);
            QVERIFY(initBook.addDocument(0, "Second Original Doc", "Second committed text") > 0);
            initBook.close();
        }

        BookDatabase crashedBook(dbPath);
        QVERIFY(crashedBook.open(password));
        BookDatabase::setRestoreFaultHookForTesting([&](const QString& s) { return s == stage; });
        QString err;
        QVERIFY(!crashedBook.restoreEncryptedBackup(backupPath, password, false, &err));
        QVERIFY(!crashedBook.isOpen());
        BookDatabase::setRestoreFaultHookForTesting(nullptr);
        QVERIFY(QFile::exists(dbPath + ".restore_journal.json"));

        // Reopening with BookDatabase::open() must detect journal, recover original state cleanly, and preserve all
        // docs
        BookDatabase reopened(dbPath);
        QVERIFY2(reopened.open(password), qPrintable(reopened.lastError()));
        QVERIFY(!QFile::exists(dbPath + ".restore_journal.json"));
        const QDir dir(tempDir.path());
        const QString base = QFileInfo(dbPath).fileName();
        QVERIFY(dir.entryList({base + ".restore.*"}, QDir::Files | QDir::Hidden).isEmpty());
        QCOMPARE(reopened.getDocuments().size(), 2);
        reopened.close();
    }

    // Interrupt before place_replacement, then RETRY restoreEncryptedBackup(backupPath) directly
    const QString retryDbPath = tempDir.filePath("book_retry.db");
    {
        BookDatabase initBook(retryDbPath);
        QVERIFY(initBook.open(password));
        QVERIFY(initBook.addDocument(0, "Before Retry 1", "Body 1") > 0);
        QVERIFY(initBook.addDocument(0, "Before Retry 2", "Body 2") > 0);
        initBook.close();
    }
    {
        BookDatabase attempt1(retryDbPath);
        QVERIFY(attempt1.open(password));
        BookDatabase::setRestoreFaultHookForTesting(
            [](const QString& s) { return s == "crash_before_place_replacement"; });
        QString err;
        QVERIFY(!attempt1.restoreEncryptedBackup(backupPath, password, false, &err));
        BookDatabase::setRestoreFaultHookForTesting(nullptr);
        QVERIFY(QFile::exists(retryDbPath + ".restore_journal.json"));
    }
    {
        BookDatabase attempt2(retryDbPath);
        QString err;
        QVERIFY2(attempt2.restoreEncryptedBackup(backupPath, password, false, &err), qPrintable(err));
        QVERIFY(attempt2.isOpen());
        QVERIFY(!QFile::exists(retryDbPath + ".restore_journal.json"));
        QCOMPARE(attempt2.getDocuments().size(), 1);
        attempt2.close();
    }
}

void TestRecoveryStore::testExistingRecoveryArtifactsNeverDestroyedAndAmbiguousStateFailsSafely() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("surviving_book.db");
    const QString legacyBakPath = dbPath + ".pre_restore.bak";
    const QString corruptPath = tempDir.filePath("corrupt.bak");
    const QString password = "preserve_recovery_pw_444";

    // Create a valid encrypted recovery file sitting ONLY at .pre_restore.bak while dbPath is missing
    {
        BookDatabase seed(dbPath);
        QVERIFY(seed.open(password));
        QVERIFY(seed.addDocument(0, "Pre-Restore Survivor", "Only surviving copy") > 0);
        seed.close();
        QVERIFY(QFile::rename(dbPath, legacyBakPath));
    }

    QFile corruptFile(corruptPath);
    QVERIFY(corruptFile.open(QIODevice::WriteOnly));
    corruptFile.write("corrupt payload");
    corruptFile.close();

    // 1. open() must refuse to create a fresh empty book when .pre_restore.bak exists and dbPath is absent
    BookDatabase guardedOpen(dbPath);
    QVERIFY(!guardedOpen.open(password));
    QVERIFY(!guardedOpen.lastError().isEmpty());
    QVERIFY(!QFile::exists(dbPath));
    QVERIFY(QFile::exists(legacyBakPath));

    // 2. Failed restore attempt with a corrupt backup must NOT destroy .pre_restore.bak
    BookDatabase failedRestore(dbPath);
    QString err;
    QVERIFY(!failedRestore.restoreEncryptedBackup(corruptPath, password, false, &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(QFile::exists(legacyBakPath));

    // 3. Restoring directly FROM .pre_restore.bak must succeed and recover the surviving document
    BookDatabase recoverFromLegacy(dbPath);
    QVERIFY2(recoverFromLegacy.restoreEncryptedBackup(legacyBakPath, password, false, &err), qPrintable(err));
    QVERIFY(recoverFromLegacy.isOpen());
    QCOMPARE(recoverFromLegacy.getDocuments().size(), 1);
    QCOMPARE(recoverFromLegacy.getDocuments().first().title, QString("Pre-Restore Survivor"));
    recoverFromLegacy.close();

    // 4. Corrupt restore journal must fail safely without deleting existing recovery files
    const QString ambigPath = tempDir.filePath("ambig.db");
    const QString ambigOrig = ambigPath + ".restore.deadbeef.orig";
    QVERIFY(QFile::copy(dbPath, ambigOrig));
    const QString ambigJournal = ambigPath + ".restore_journal.json";
    {
        QFile jf(ambigJournal);
        QVERIFY(jf.open(QIODevice::WriteOnly));
        jf.write("{truncated-corrupt-json");
        jf.close();
    }
    BookDatabase ambigBook(ambigPath);
    QVERIFY(!ambigBook.open(password));
    QVERIFY(!ambigBook.lastError().isEmpty());
    QVERIFY(QFile::exists(ambigJournal));
    QVERIFY(QFile::exists(ambigOrig));

    // 5. Journal referencing missing .orig copy must fail safely and delete nothing
    QVERIFY(QFile::remove(ambigJournal));
    const QString validOpId = "0123456789abcdef0123456789abcdef";
    QJsonObject validButMissingOrig;
    validButMissingOrig["version"] = 1;
    validButMissingOrig["op_id"] = validOpId;
    validButMissingOrig["phase"] = "placing_replacement";
    validButMissingOrig["target_path"] = ambigPath;
    validButMissingOrig["staged_new_path"] = QString("%1.restore.%2.new").arg(ambigPath, validOpId);
    validButMissingOrig["rollback_db_path"] = QString("%1.restore.%2.orig").arg(ambigPath, validOpId);
    validButMissingOrig["rollback_wal_path"] = QString("%1.restore.%2.orig-wal").arg(ambigPath, validOpId);
    validButMissingOrig["rollback_shm_path"] = QString("%1.restore.%2.orig-shm").arg(ambigPath, validOpId);
    validButMissingOrig["had_original_db"] = true;
    validButMissingOrig["had_original_wal"] = false;
    validButMissingOrig["had_original_shm"] = false;
    {
        QFile jf(ambigJournal);
        QVERIFY(jf.open(QIODevice::WriteOnly));
        jf.write(QJsonDocument(validButMissingOrig).toJson(QJsonDocument::Compact));
        jf.close();
    }
    QVERIFY(QFile::copy(dbPath, ambigPath));
    BookDatabase missingRollbackBook(ambigPath);
    QVERIFY(!missingRollbackBook.open(password));
    QVERIFY(!missingRollbackBook.lastError().isEmpty());
    QVERIFY(QFile::exists(ambigJournal));
    QVERIFY(QFile::exists(ambigPath));
    QVERIFY(QFile::exists(ambigOrig));
}

void TestRecoveryStore::testDurabilitySyncAndCleanupErrorPropagation() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("durability_book.db");
    const QString backupPath = tempDir.filePath("durability_backup.db");
    const QString failedBackupPath = tempDir.filePath("durability_backup_fail.db");
    const QString password = "durability_pw_555";

    // Seed original DB + valid older backup
    {
        BookDatabase seed(dbPath);
        QVERIFY(seed.open(password));
        seed.setSetting("book", 0, "epoch", "backup_v1");
        QString err;
        QVERIFY2(seed.createEncryptedBackup(backupPath, &err), qPrintable(err));
        seed.setSetting("book", 0, "epoch", "live_v2");

        // 1. Backup directory sync failure must propagate error and not report success
        BookDatabase::setRestoreFaultHookForTesting([](const QString& s) { return s == "sync_backup_dir"; });
        QVERIFY(!seed.createEncryptedBackup(failedBackupPath, &err));
        QVERIFY(err.contains("sync", Qt::CaseInsensitive));
        BookDatabase::setRestoreFaultHookForTesting(nullptr);
        seed.close();
    }

    // 2. Restore failure on sync_after_place_replacement must roll back cleanly to live_v2
    {
        BookDatabase book(dbPath);
        QVERIFY(book.open(password));
        BookDatabase::setRestoreFaultHookForTesting(
            [](const QString& s) { return s == "sync_after_place_replacement"; });
        QString err;
        QVERIFY(!book.restoreEncryptedBackup(backupPath, password, false, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(book.isOpen());
        QCOMPARE(book.getSetting("book", 0, "epoch"), QString("live_v2"));
        BookDatabase::setRestoreFaultHookForTesting(nullptr);
        book.close();
    }

    // 3. Journal removal failure keeps rollback copies intact and allows recovery to original live_v2 on reopen
    {
        BookDatabase book(dbPath);
        QVERIFY(book.open(password));
        BookDatabase::setRestoreFaultHookForTesting([](const QString& s) { return s == "remove_restore_journal"; });
        QString err;
        QVERIFY(!book.restoreEncryptedBackup(backupPath, password, false, &err));
        QVERIFY(!book.isOpen());
        QVERIFY(QFile::exists(dbPath + ".restore_journal.json"));
        const QDir dir(tempDir.path());
        QVERIFY(!dir.entryList({QStringLiteral("durability_book.db.restore.*.orig")}, QDir::Files | QDir::Hidden)
                     .isEmpty());
        BookDatabase::setRestoreFaultHookForTesting(nullptr);

        BookDatabase reopen(dbPath);
        QVERIFY2(reopen.open(password), qPrintable(reopen.lastError()));
        QCOMPARE(reopen.getSetting("book", 0, "epoch"), QString("live_v2"));
        QVERIFY(!QFile::exists(dbPath + ".restore_journal.json"));
        QVERIFY(dir.entryList({QStringLiteral("durability_book.db.restore.*")}, QDir::Files | QDir::Hidden).isEmpty());
        reopen.close();
    }

    // 4. Failure of directory sync after journal unlink MUST retain rollback copies and return false
    {
        const QString barrierDb = tempDir.filePath("barrier_book.db");
        QVERIFY(QFile::copy(dbPath, barrierDb));
        BookDatabase book(barrierDb);
        QVERIFY(book.open(password));
        BookDatabase::setRestoreFaultHookForTesting([](const QString& s) { return s == "sync_after_journal_remove"; });
        QString err;
        QVERIFY(!book.restoreEncryptedBackup(backupPath, password, false, &err));
        QVERIFY(!book.isOpen());
        QVERIFY(err.contains("durability barrier", Qt::CaseInsensitive) || err.contains("sync", Qt::CaseInsensitive));
        QVERIFY(!QFile::exists(barrierDb + ".restore_journal.json"));
        const QDir dir(tempDir.path());
        const QStringList retained =
            dir.entryList({QStringLiteral("barrier_book.db.restore.*.orig")}, QDir::Files | QDir::Hidden);
        QVERIFY(!retained.isEmpty());
        BookDatabase::setRestoreFaultHookForTesting(nullptr);

        BookDatabase verifyCommitted(barrierDb);
        QVERIFY2(verifyCommitted.open(password), qPrintable(verifyCommitted.lastError()));
        QCOMPARE(verifyCommitted.getSetting("book", 0, "epoch"), QString("backup_v1"));
        verifyCommitted.close();
    }

    // 5. Post-commit cleanup and final sync failures must propagate failure (never return misleading true)
    for (const QString& stage : {QStringLiteral("cleanup_rollback_db"), QStringLiteral("sync_after_restore_cleanup")}) {
        const QString cleanDb = tempDir.filePath(QString("cleanup_%1.db").arg(stage));
        QVERIFY(QFile::copy(dbPath, cleanDb));
        BookDatabase book(cleanDb);
        QVERIFY(book.open(password));
        BookDatabase::setRestoreFaultHookForTesting([&](const QString& s) { return s == stage; });
        QString err;
        QVERIFY(!book.restoreEncryptedBackup(backupPath, password, false, &err));
        QVERIFY(!book.isOpen());
        QVERIFY(!err.isEmpty());
        BookDatabase::setRestoreFaultHookForTesting(nullptr);
    }

    // 6. Recovery durability barriers: failure on sync_recovery_restore_original keeps journal,
    //    and failure on sync_recovery_after_journal_remove retains .aborted/.new files
    {
        const QString recDb = tempDir.filePath("rec_barrier.db");
        QVERIFY(QFile::copy(dbPath, recDb));
        BookDatabase crasher(recDb);
        QVERIFY(crasher.open(password));
        BookDatabase::setRestoreFaultHookForTesting([](const QString& s) { return s == "crash_during_verifying"; });
        QString err;
        QVERIFY(!crasher.restoreEncryptedBackup(backupPath, password, false, &err));
        BookDatabase::setRestoreFaultHookForTesting(nullptr);

        BookDatabase rec1(recDb);
        BookDatabase::setRestoreFaultHookForTesting(
            [](const QString& s) { return s == "sync_recovery_restore_original"; });
        QVERIFY(!rec1.open(password));
        QVERIFY(QFile::exists(recDb + ".restore_journal.json"));
        BookDatabase::setRestoreFaultHookForTesting(nullptr);

        BookDatabase rec2(recDb);
        BookDatabase::setRestoreFaultHookForTesting(
            [](const QString& s) { return s == "sync_recovery_after_journal_remove"; });
        QVERIFY(!rec2.open(password));
        QVERIFY(!QFile::exists(recDb + ".restore_journal.json"));
        const QDir dir(tempDir.path());
        QVERIFY(!dir.entryList({QStringLiteral("rec_barrier.db.restore.*")}, QDir::Files | QDir::Hidden).isEmpty());
        BookDatabase::setRestoreFaultHookForTesting(nullptr);

        BookDatabase verifyRec(recDb);
        QVERIFY2(verifyRec.open(password), qPrintable(verifyRec.lastError()));
        QCOMPARE(verifyRec.getSetting("book", 0, "epoch"), QString("live_v2"));
        verifyRec.close();
    }

    // 7. Interruption during recovery itself after rollback DB moved while rollback WAL remains at .orig-wal
    {
        const QString walRecDb = tempDir.filePath("wal_partial_recovery.db");
        QVERIFY(QFile::copy(dbPath, walRecDb));

        {
            BookDatabase walSetup(walRecDb);
            QVERIFY(walSetup.open(password));
            db::Database raw(reinterpret_cast<sqlite3*>(walSetup.getDatabaseHandleForTesting()));
            QVERIFY(raw.execute("PRAGMA journal_mode = WAL;"));
            walSetup.close();
        }

        // Hold an older WAL reader while committing a new setting so -wal sidecar stays uncheckpointed on disk
        const QString crashTarget = tempDir.filePath("wal_partial_crash_target.db");
        sqlite3* holder = nullptr;
        QCOMPARE(sqlite3_open(walRecDb.toUtf8().constData(), &holder), SQLITE_OK);
        db::Database hdb(holder);
        QVERIFY(hdb.execute(QString("PRAGMA key = '%1';").arg(password)));
        int cnt = 0;
        {
            BookDatabase walWriter(walRecDb);
            QVERIFY(walWriter.open(password));
            walWriter.setSetting("book", 0, "wal_epoch", "must_survive_recovery");
            QVERIFY(hdb.execute("BEGIN;"));
            QVERIFY(hdb.queryInt("SELECT COUNT(*) FROM sqlite_master;", cnt));
            QVERIFY(cnt > 0);
            walWriter.close();
        }
        QVERIFY(QFile::exists(walRecDb + "-wal"));
        QVERIFY(QFileInfo(walRecDb + "-wal").size() > 0);
        QVERIFY(QFile::copy(walRecDb, crashTarget));
        QVERIFY(QFile::copy(walRecDb + "-wal", crashTarget + "-wal"));
        if (QFile::exists(walRecDb + "-shm")) {
            QVERIFY(QFile::copy(walRecDb + "-shm", crashTarget + "-shm"));
        }
        QVERIFY(hdb.execute("COMMIT;"));
        sqlite3_close(holder);

        // Cold restore fails during verifying -> places journal in verifying phase
        {
            BookDatabase coldRestore(crashTarget);
            BookDatabase::setRestoreFaultHookForTesting([](const QString& s) { return s == "crash_during_verifying"; });
            QString err;
            QVERIFY(!coldRestore.restoreEncryptedBackup(backupPath, password, false, &err));
            BookDatabase::setRestoreFaultHookForTesting(nullptr);
            QVERIFY(QFile::exists(crashTarget + ".restore_journal.json"));
        }

        // First recovery crashes RIGHT AFTER moving .orig -> db, leaving .orig-wal unmoved and phase == rolling_back
        {
            BookDatabase partialRec(crashTarget);
            BookDatabase::setRestoreFaultHookForTesting(
                [](const QString& s) { return s == "crash_recovery_after_rollback_db"; });
            QString err;
            QVERIFY(!partialRec.recoverInterruptedRestore(password, &err));
            BookDatabase::setRestoreFaultHookForTesting(nullptr);
            QVERIFY(QFile::exists(crashTarget + ".restore_journal.json"));
            const QDir dir(tempDir.path());
            QVERIFY(!dir.entryList({QStringLiteral("wal_partial_crash_target.db.restore.*.orig-wal")},
                                   QDir::Files | QDir::Hidden)
                         .isEmpty());
        }

        // Subsequent open must complete the remaining .orig-wal -> -wal move and preserve must_survive_recovery!
        {
            BookDatabase finalOpen(crashTarget);
            QVERIFY2(finalOpen.open(password), qPrintable(finalOpen.lastError()));
            QCOMPARE(finalOpen.getSetting("book", 0, "wal_epoch"), QString("must_survive_recovery"));
            QVERIFY(!QFile::exists(crashTarget + ".restore_journal.json"));
            const QDir dir(tempDir.path());
            QVERIFY(dir.entryList({QStringLiteral("wal_partial_crash_target.db.restore.*")}, QDir::Files | QDir::Hidden)
                        .isEmpty());
            finalOpen.close();
        }
    }
}

void TestRecoveryStore::testRestoreJournalStrictValidationAndSymlinkRejection() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("strict_journal.db");
    const QString journalPath = dbPath + ".restore_journal.json";
    const QString innocentFile = tempDir.filePath("innocent.txt");
    const QString password = "strict_journal_pw_666";

    {
        BookDatabase initBook(dbPath);
        QVERIFY(initBook.open(password));
        initBook.setSetting("book", 0, "marker", "untouched_live");
        initBook.close();
    }

    auto resetSentinel = [&]() {
        QFile::remove(innocentFile);
        QFile f(innocentFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("DO NOT TOUCH THIS SENTINEL");
        f.close();
    };
    auto readSentinel = [&]() {
        QFile f(innocentFile);
        if (!f.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return f.readAll();
    };
    auto writeRawJournal = [&](const QJsonObject& obj) {
        QFile::remove(journalPath);
        QFile jf(journalPath);
        QVERIFY(jf.open(QIODevice::WriteOnly));
        jf.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        jf.close();
    };

    resetSentinel();

    const QString validOpId = "abcdef0123456789abcdef0123456789";

    // 1. Invalid op_id variants (path traversal, short string, uppercase hex)
    for (const QString& badOp :
         {QStringLiteral("../evil"), QStringLiteral("missingop"), QStringLiteral("ABCDEF0123456789ABCDEF0123456789")}) {
        QJsonObject obj;
        obj["version"] = 1;
        obj["op_id"] = badOp;
        obj["phase"] = "staging";
        obj["target_path"] = dbPath;
        obj["staged_new_path"] = innocentFile;
        obj["rollback_db_path"] = QString("%1.restore.%2.orig").arg(dbPath, badOp);
        obj["rollback_wal_path"] = QString("%1.restore.%2.orig-wal").arg(dbPath, badOp);
        obj["rollback_shm_path"] = QString("%1.restore.%2.orig-shm").arg(dbPath, badOp);
        obj["had_original_db"] = true;
        obj["had_original_wal"] = false;
        obj["had_original_shm"] = false;
        writeRawJournal(obj);

        BookDatabase check(dbPath);
        QVERIFY(!check.open(password));
        QCOMPARE(readSentinel(), QByteArray("DO NOT TOUCH THIS SENTINEL"));
        QVERIFY(QFile::exists(dbPath));
    }

    // 2. Arbitrary / unrelated staged_new_path in staging phase must NEVER be deleted
    {
        QJsonObject obj;
        obj["version"] = 1;
        obj["op_id"] = validOpId;
        obj["phase"] = "staging";
        obj["target_path"] = dbPath;
        obj["staged_new_path"] = innocentFile;
        obj["rollback_db_path"] = QString("%1.restore.%2.orig").arg(dbPath, validOpId);
        obj["rollback_wal_path"] = QString("%1.restore.%2.orig-wal").arg(dbPath, validOpId);
        obj["rollback_shm_path"] = QString("%1.restore.%2.orig-shm").arg(dbPath, validOpId);
        obj["had_original_db"] = true;
        obj["had_original_wal"] = false;
        obj["had_original_shm"] = false;
        writeRawJournal(obj);

        BookDatabase check(dbPath);
        QVERIFY(!check.open(password));
        QCOMPARE(readSentinel(), QByteArray("DO NOT TOUCH THIS SENTINEL"));
    }

    // 3. Path alias (staged_new_path == dbPath) must be rejected without touching dbPath
    {
        QJsonObject obj;
        obj["version"] = 1;
        obj["op_id"] = validOpId;
        obj["phase"] = "staging";
        obj["target_path"] = dbPath;
        obj["staged_new_path"] = dbPath;
        obj["rollback_db_path"] = QString("%1.restore.%2.orig").arg(dbPath, validOpId);
        obj["rollback_wal_path"] = QString("%1.restore.%2.orig-wal").arg(dbPath, validOpId);
        obj["rollback_shm_path"] = QString("%1.restore.%2.orig-shm").arg(dbPath, validOpId);
        obj["had_original_db"] = true;
        obj["had_original_wal"] = false;
        obj["had_original_shm"] = false;
        writeRawJournal(obj);

        BookDatabase check(dbPath);
        QVERIFY(!check.open(password));
        QVERIFY(QFile::exists(dbPath));
    }

    // 4. Arbitrary rollback_db_path / rollback_wal_path in moving_original & placing_replacement must never move/delete
    // innocentFile
    for (const QString& phase : {QStringLiteral("moving_original"), QStringLiteral("placing_replacement")}) {
        QJsonObject obj;
        obj["version"] = 1;
        obj["op_id"] = validOpId;
        obj["phase"] = phase;
        obj["target_path"] = dbPath;
        obj["staged_new_path"] = QString("%1.restore.%2.new").arg(dbPath, validOpId);
        obj["rollback_db_path"] = innocentFile;
        obj["rollback_wal_path"] = QString("%1.restore.%2.orig-wal").arg(dbPath, validOpId);
        obj["rollback_shm_path"] = QString("%1.restore.%2.orig-shm").arg(dbPath, validOpId);
        obj["had_original_db"] = true;
        obj["had_original_wal"] = false;
        obj["had_original_shm"] = false;
        writeRawJournal(obj);

        BookDatabase check(dbPath);
        QVERIFY(!check.open(password));
        QCOMPARE(readSentinel(), QByteArray("DO NOT TOUCH THIS SENTINEL"));
    }

    // 5. Inconsistent booleans (had_original_db=false with had_original_wal=true) must be rejected
    {
        QJsonObject obj;
        obj["version"] = 1;
        obj["op_id"] = validOpId;
        obj["phase"] = "placing_replacement";
        obj["target_path"] = dbPath;
        obj["staged_new_path"] = QString("%1.restore.%2.new").arg(dbPath, validOpId);
        obj["rollback_db_path"] = QString("%1.restore.%2.orig").arg(dbPath, validOpId);
        obj["rollback_wal_path"] = QString("%1.restore.%2.orig-wal").arg(dbPath, validOpId);
        obj["rollback_shm_path"] = QString("%1.restore.%2.orig-shm").arg(dbPath, validOpId);
        obj["had_original_db"] = false;
        obj["had_original_wal"] = true;
        obj["had_original_shm"] = false;
        writeRawJournal(obj);

        BookDatabase check(dbPath);
        QVERIFY(!check.open(password));
        QVERIFY(QFile::exists(dbPath));
    }

    // 6. Symlinked staged_new_path and symlinked journal (both live & dangling) must be rejected safely
    {
        QJsonObject validObj;
        validObj["version"] = 1;
        validObj["op_id"] = validOpId;
        validObj["phase"] = "staging";
        validObj["target_path"] = dbPath;
        const QString derivedStaged = QString("%1.restore.%2.new").arg(dbPath, validOpId);
        validObj["staged_new_path"] = derivedStaged;
        validObj["rollback_db_path"] = QString("%1.restore.%2.orig").arg(dbPath, validOpId);
        validObj["rollback_wal_path"] = QString("%1.restore.%2.orig-wal").arg(dbPath, validOpId);
        validObj["rollback_shm_path"] = QString("%1.restore.%2.orig-shm").arg(dbPath, validOpId);
        validObj["had_original_db"] = true;
        validObj["had_original_wal"] = false;
        validObj["had_original_shm"] = false;
        writeRawJournal(validObj);

        QFile::remove(derivedStaged);
        if (QFile::link(innocentFile, derivedStaged)) {
            BookDatabase checkSymArtifact(dbPath);
            QVERIFY(!checkSymArtifact.open(password));
            QCOMPARE(readSentinel(), QByteArray("DO NOT TOUCH THIS SENTINEL"));
            QVERIFY(QFileInfo(derivedStaged).isSymLink());
            QFile::remove(derivedStaged);
        }

        // Replace journal itself with a dangling symlink
        QFile::remove(journalPath);
        if (QFile::link(tempDir.filePath("nonexistent_journal_target.json"), journalPath)) {
            BookDatabase checkSymJournal(dbPath);
            QVERIFY(!checkSymJournal.open(password));
            QVERIFY(QFileInfo(journalPath).isSymLink());
            QFile::remove(journalPath);
        }
    }

    // Removing the test-crafted invalid journal allows the untouched original DB to open normally
    QFile::remove(journalPath);
    BookDatabase cleanOpen(dbPath);
    QVERIFY2(cleanOpen.open(password), qPrintable(cleanOpen.lastError()));
    QCOMPARE(cleanOpen.getSetting("book", 0, "marker"), QString("untouched_live"));
    cleanOpen.close();
}

void TestRecoveryStore::testBackupSnapshotConsistencyUnderCompetingWrites() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath("snapshot_consistency.db");
    const QString backupPath = tempDir.filePath("snapshot_backup.db");
    const QString password = "snapshot_pw_777";

    BookDatabase book(dbPath);
    QVERIFY(book.open(password));
    book.setSetting("book", 0, "baseline", "initial_v1");

    QString err;
    QVERIFY2(book.createEncryptedBackup(backupPath, &err), qPrintable(err));

    // 1. Refuse backup when this same connection has an active uncommitted transaction
    {
        db::Database raw(reinterpret_cast<sqlite3*>(book.getDatabaseHandleForTesting()));
        QVERIFY2(raw.execute("BEGIN;", &err), qPrintable(err));
        QVERIFY2(raw.execute("INSERT OR REPLACE INTO settings (scope, target_id, key, value) VALUES ('book', 0, "
                             "'uncommitted', 'should_fail');",
                             &err),
                 qPrintable(err));
        QVERIFY(!book.createEncryptedBackup(backupPath, &err));
        QVERIFY(err.contains("uncommitted", Qt::CaseInsensitive));
        QVERIFY2(raw.execute("ROLLBACK;", &err), qPrintable(err));
    }

    // 2. Competing connection holding BEGIN IMMEDIATE in DELETE mode blocks backup & preserves existing backup
    {
        sqlite3* writerHandle = nullptr;
        QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &writerHandle), SQLITE_OK);
        db::Database writerDb(writerHandle);
        QVERIFY2(writerDb.execute(QString("PRAGMA key = '%1';").arg(password), &err), qPrintable(err));
        QVERIFY2(writerDb.execute("BEGIN IMMEDIATE;", &err), qPrintable(err));
        QVERIFY2(writerDb.execute("INSERT OR REPLACE INTO settings (scope, target_id, key, value) VALUES ('book', 0, "
                                  "'delete_mode_tx', 'in_flight');",
                                  &err),
                 qPrintable(err));

        QVERIFY(!book.createEncryptedBackup(backupPath, &err));
        QVERIFY(!err.isEmpty());

        QVERIFY2(writerDb.execute("COMMIT;", &err), qPrintable(err));
        sqlite3_close(writerHandle);

        QVERIFY2(book.createEncryptedBackup(backupPath, &err), qPrintable(err));
    }

    // 3. Switch to WAL mode: active competing WAL write transaction must be refused
    {
        db::Database primaryDb(reinterpret_cast<sqlite3*>(book.getDatabaseHandleForTesting()));
        QVERIFY2(primaryDb.execute("PRAGMA journal_mode = WAL;", &err), qPrintable(err));

        sqlite3* walWriter = nullptr;
        QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &walWriter), SQLITE_OK);
        db::Database walWriterDb(walWriter);
        QVERIFY2(walWriterDb.execute(QString("PRAGMA key = '%1';").arg(password), &err), qPrintable(err));
        QVERIFY2(walWriterDb.execute("BEGIN IMMEDIATE;", &err), qPrintable(err));
        QVERIFY2(
            walWriterDb.execute("INSERT OR REPLACE INTO settings (scope, target_id, key, value) VALUES ('book', 0, "
                                "'wal_active_tx', 'uncommitted');",
                                &err),
            qPrintable(err));

        QVERIFY(!book.createEncryptedBackup(backupPath, &err));
        QVERIFY(!err.isEmpty());

        // Existing valid backup must NOT have been overwritten
        const QString verifyCopy = tempDir.filePath("preserved_wal.db");
        QVERIFY(QFile::copy(backupPath, verifyCopy));
        BookDatabase checkBackup(verifyCopy);
        QVERIFY(checkBackup.open(password));
        QCOMPARE(checkBackup.getSetting("book", 0, "delete_mode_tx"), QString("in_flight"));
        QCOMPARE(checkBackup.getSetting("book", 0, "wal_active_tx"), QString());
        checkBackup.close();

        QVERIFY2(walWriterDb.execute("COMMIT;", &err), qPrintable(err));
        sqlite3_close(walWriter);
    }

    // 4. Simulated race window between checkpoint and exclusive lock: retry loop detects non-empty WAL and captures
    //    the committed state consistently on retry
    {
        sqlite3* racingWriter = nullptr;
        QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &racingWriter), SQLITE_OK);
        db::Database racingDb(racingWriter);
        QVERIFY2(racingDb.execute(QString("PRAGMA key = '%1';").arg(password), &err), qPrintable(err));

        int raceInjections = 0;
        BookDatabase::setRestoreFaultHookForTesting([&](const QString& stage) {
            if (stage == "after_backup_checkpoint_before_lock" && raceInjections == 0) {
                ++raceInjections;
                QString raceErr;
                Q_UNUSED(racingDb.execute(
                    "INSERT OR REPLACE INTO settings (scope, target_id, key, value) VALUES ('book', 0, "
                    "'raced_commit', 'captured_on_retry');",
                    &raceErr));
            }
            return false;
        });

        QVERIFY2(book.createEncryptedBackup(backupPath, &err), qPrintable(err));
        QCOMPARE(raceInjections, 1);
        BookDatabase::setRestoreFaultHookForTesting(nullptr);
        sqlite3_close(racingWriter);

        const QString retryVerifyCopy = tempDir.filePath("retry_verify.db");
        QVERIFY(QFile::copy(backupPath, retryVerifyCopy));
        BookDatabase checkRetry(retryVerifyCopy);
        QVERIFY(checkRetry.open(password));
        QCOMPARE(checkRetry.getSetting("book", 0, "raced_commit"), QString("captured_on_retry"));
        checkRetry.close();
    }

    // 5. Persistent concurrent WAL writes on every snapshot attempt refuse cleanly after bounded retries without
    //    overwriting the existing valid backup
    {
        sqlite3* floodWriter = nullptr;
        QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &floodWriter), SQLITE_OK);
        db::Database floodDb(floodWriter);
        QVERIFY2(floodDb.execute(QString("PRAGMA key = '%1';").arg(password), &err), qPrintable(err));

        int seq = 0;
        BookDatabase::setRestoreFaultHookForTesting([&](const QString& stage) {
            if (stage == "after_backup_checkpoint_before_lock") {
                ++seq;
                QString floodErr;
                Q_UNUSED(floodDb.execute(
                    QString("INSERT OR REPLACE INTO settings (scope, target_id, key, value) VALUES ('book', 0, "
                            "'flood_seq', 'v%1');")
                        .arg(seq),
                    &floodErr));
            }
            return false;
        });

        QVERIFY(!book.createEncryptedBackup(backupPath, &err));
        QVERIFY(!err.isEmpty());
        QCOMPARE(seq, 3);
        BookDatabase::setRestoreFaultHookForTesting(nullptr);
        sqlite3_close(floodWriter);

        // Existing valid backup still holds the pre-flood consistent state without flood_seq
        const QString preservedCopy = tempDir.filePath("flood_preserved.db");
        QVERIFY(QFile::copy(backupPath, preservedCopy));
        BookDatabase checkPreserved(preservedCopy);
        QVERIFY(checkPreserved.open(password));
        QCOMPARE(checkPreserved.getSetting("book", 0, "raced_commit"), QString("captured_on_retry"));
        QCOMPARE(checkPreserved.getSetting("book", 0, "flood_seq"), QString());
        checkPreserved.close();
    }

    book.close();
}

QTEST_MAIN(TestRecoveryStore)
#include "test_RecoveryStore.moc"
