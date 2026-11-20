#include <sqlcipher/sqlite3.h>

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

QTEST_MAIN(TestRecoveryStore)
#include "test_RecoveryStore.moc"
