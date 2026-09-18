#include <sqlcipher/sqlite3.h>

#include <QCoreApplication>
#include <QtTest>

#include "../src/db/Database.h"
#include "../src/db/MigrationFactory.h"
#include "../src/db/Migrations.h"

class TestMigrations : public QObject {
    Q_OBJECT

   private slots:
    void testFreshDatabaseMigration();
    void testAlreadyCurrentNoOp();
    void testPartialMigration();
    void testFailingMigrationRollback();
    void testFailingAlterRollback();
    void testVersionDisagreement();
    void testForeignKeysEnabled();
    void testFreshSchemaEquivalence();
    void testLegacySeeding();
    void testFailingLegacySeeding();
};

void TestMigrations::testFreshDatabaseMigration() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner;
    runner.addMigration({0, 1, "test 1", [](db::Database& d) { return d.execute("CREATE TABLE t1 (id INTEGER);"); }});

    QString error;
    QVERIFY2(runner.run(db, &error), qPrintable(error));

    int version = 0;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 1);

    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 1);

    sqlite3_close(dbHandle);
}

void TestMigrations::testAlreadyCurrentNoOp() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner;
    runner.addMigration({0, 1, "test 1", [](db::Database& d) { return d.execute("CREATE TABLE t1 (id INTEGER);"); }});

    QVERIFY(runner.run(db));

    // Add a fake failure to test 1 if it runs again
    db::MigrationRunner runner2;
    runner2.addMigration({0, 1, "test 1", [](db::Database& d) { return false; }});

    QVERIFY(runner2.run(db));  // Should be no-op, so it shouldn't fail

    sqlite3_close(dbHandle);
}

void TestMigrations::testPartialMigration() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner;
    runner.addMigration({0, 1, "test 1", [](db::Database& d) { return d.execute("CREATE TABLE t1 (id INTEGER);"); }});
    QVERIFY(runner.run(db));

    db::MigrationRunner runner2;
    runner2.addMigration({0, 1, "test 1", [](db::Database& d) {
                              return false;  // Should not be run
                          }});
    runner2.addMigration({1, 2, "test 2", [](db::Database& d) { return d.execute("CREATE TABLE t2 (id INTEGER);"); }});

    QString error;
    QVERIFY2(runner2.run(db, &error), qPrintable(error));

    int version = 0;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 2);

    sqlite3_close(dbHandle);
}

void TestMigrations::testFailingMigrationRollback() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner;
    runner.addMigration({0, 1, "test 1", [](db::Database& d) {
                             d.execute("CREATE TABLE t1 (id INTEGER);");  // This should be rolled back
                             return false;                                // Deliberate failure
                         }});

    QString error;
    QVERIFY(!runner.run(db, &error));

    // t1 should not exist
    int count = 0;
    QVERIFY(db.queryInt("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='t1';", count));
    QCOMPARE(count, 0);

    // version should be 0
    int version = -1;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 0);

    sqlite3_close(dbHandle);
}

#include "../src/BookDatabase.h"
void TestMigrations::testForeignKeysEnabled() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    // Test that the production connection correctly enabled foreign keys
    db::Database dbAccess(reinterpret_cast<sqlite3*>(db.getDatabaseHandleForTesting()));  // We'll add this accessor

    int enabled = 0;
    QVERIFY(dbAccess.queryInt("PRAGMA foreign_keys;", enabled));
    QCOMPARE(enabled, 1);

    dbAccess.execute("CREATE TABLE p (id INTEGER PRIMARY KEY);");
    dbAccess.execute("CREATE TABLE c (id INTEGER, p_id INTEGER REFERENCES p(id));");

    // Should fail
    QString error;
    QVERIFY(!dbAccess.execute("INSERT INTO c (id, p_id) VALUES (1, 1);", &error));
    QVERIFY(error.contains("FOREIGN KEY constraint failed"));

    db.close();
}

QTEST_MAIN(TestMigrations)
#include "test_Migrations.moc"

void TestMigrations::testFreshSchemaEquivalence() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString error;
    QVERIFY2(runner.run(db, &error), qPrintable(error));

    // Check some specific columns exist
    auto checkColumn = [&](const QString& table, const QString& column) {
        bool found = false;
        QString sql = "PRAGMA table_info(" + table + ");";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db.handle(), sql.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                QString name = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
                if (name == column) {
                    found = true;
                    break;
                }
            }
            sqlite3_finalize(stmt);
        }
        return found;
    };

    // Core V21 tables
    QVERIFY(checkColumn("schema_version", "version"));
    QVERIFY(checkColumn("schema_version", "applied_at"));

    QVERIFY(checkColumn("documents", "parent_id"));
    QVERIFY(checkColumn("documents", "folder_id"));



    QVERIFY(checkColumn("notes", "folder_id"));


    QVERIFY(checkColumn("messages", "folder_id"));

    QVERIFY(checkColumn("notifications", "target_id"));
    QVERIFY(checkColumn("notifications", "target_type"));

    QVERIFY(checkColumn("queue", "target_type"));

    sqlite3_close(dbHandle);
}

void TestMigrations::testFailingAlterRollback() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db.execute(
        "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    db.execute("INSERT INTO schema_version (version) VALUES (2);");
    db.execute("PRAGMA user_version = 2;");
    db.execute("CREATE TABLE documents (id INTEGER);");

    db::MigrationRunner runner;
    runner.addMigration(
        {2, 3, "test alter", [](db::Database& d) {
             d.execute("CREATE TABLE t1 (id INTEGER);");
             // This will fail because syntax error
             return d.execute("CREATE TABLE schema_version (id INTEGER);");  // Will fail because table already exists
         }});

    QString error;
    QVERIFY(!runner.run(db, &error));

    // t1 should not exist because of rollback
    int count = 0;
    QVERIFY(db.queryInt("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='t1';", count));
    QCOMPARE(count, 0);

    // version should be 2
    int version = -1;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 2);

    sqlite3_close(dbHandle);
}

void TestMigrations::testVersionDisagreement() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db.execute(
        "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    db.execute("INSERT INTO schema_version (version) VALUES (2);");
    db.execute("PRAGMA user_version = 3;");  // Disagreement!

    db::MigrationRunner runner;
    runner.addMigration({3, 4, "test", [](db::Database& d) { return d.execute("CREATE TABLE t1 (id INTEGER);"); }});

    QString error;
    QVERIFY(!runner.run(db, &error));
    QVERIFY(error.contains("Version disagreement"));

    // Also test zero disagreement
    sqlite3* dbHandle2;
    sqlite3_open(":memory:", &dbHandle2);
    db::Database db2(dbHandle2);
    db2.execute(
        "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    db2.execute("PRAGMA user_version = 2;");  // Disagreement with zero/empty schema_version

    db::MigrationRunner runner2;
    runner2.addMigration({2, 3, "test", [](db::Database& d) { return d.execute("CREATE TABLE t2 (id INTEGER);"); }});
    QVERIFY(!runner2.run(db2, &error));
    QVERIFY(error.contains("Version disagreement"));

    // Ensure rejected state does not execute a migration or change either version marker.
    int count = 0;
    QVERIFY(db2.queryInt("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='t2';", count));
    QCOMPARE(count, 0);  // No migration

    int version = -1;
    QVERIFY(db2.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 2);  // No change

    version = -1;
    QVERIFY(db2.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 0);  // Still 0

    sqlite3_close(dbHandle2);

    // Test the other way: schema_version ahead of pragma
    sqlite3* dbHandle3;
    sqlite3_open(":memory:", &dbHandle3);
    db::Database db3(dbHandle3);
    db3.execute(
        "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    db3.execute("INSERT INTO schema_version (version) VALUES (3);");
    db3.execute("PRAGMA user_version = 0;");  // Disagreement with zero pragma_version
    QVERIFY(!runner.run(db3, &error));
    QVERIFY(error.contains("Version disagreement"));

    // Test read failure
    sqlite3* dbHandle4;
    sqlite3_open(":memory:", &dbHandle4);
    db::Database db4(dbHandle4);
    db4.execute(
        "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    db4.execute("INSERT INTO schema_version (version) VALUES (2);");
    db4.execute("PRAGMA user_version = 2;");

    sqlite3_set_authorizer(
        dbHandle4,
        [](void*, int action, const char*, const char*, const char*, const char*) {
            if (action == SQLITE_READ) {
                return SQLITE_DENY;
            }
            return SQLITE_OK;
        },
        nullptr);
    QVERIFY(!runner.run(db4, &error));

    sqlite3_close(dbHandle3);
    sqlite3_close(dbHandle4);


    sqlite3_close(dbHandle);
}

void TestMigrations::testLegacySeeding() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db.execute("PRAGMA user_version = 2;");  // No schema_version table yet

    db::MigrationRunner runner;
    runner.addMigration({2, 3, "test", [](db::Database& d) { return d.execute("CREATE TABLE t1 (id INTEGER);"); }});

    QString error;
    QVERIFY(runner.run(db, &error));

    // Verify it migrated and seeded correctly
    int version = -1;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 3);

    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 3);

    sqlite3_close(dbHandle);
}

void TestMigrations::testFailingLegacySeeding() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db.execute("PRAGMA user_version = 2;");  // No schema_version table yet

    // Simulate an error during legacy seeding by causing INSERT to fail
    sqlite3_set_authorizer(
        dbHandle,
        [](void*, int action, const char*, const char*, const char*, const char*) {
            if (action == SQLITE_INSERT) {
                return SQLITE_DENY;
            }
            return SQLITE_OK;
        },
        nullptr);

    db::MigrationRunner runner;
    runner.addMigration({2, 3, "test", [](db::Database& d) { return d.execute("CREATE TABLE t1 (id INTEGER);"); }});

    QString error;
    QVERIFY(!runner.run(db, &error));

    // Remove authorizer
    sqlite3_set_authorizer(dbHandle, nullptr, nullptr);

    // Verify it failed completely
    int count = 0;
    QVERIFY(db.queryInt("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='schema_version';", count));
    QCOMPARE(count, 0);  // schema_version should not exist

    int version = -1;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 2);  // Unchanged

    // Reopen and try again with no authorizer, should succeed
    QVERIFY(runner.run(db, &error));

    sqlite3_close(dbHandle);
}
