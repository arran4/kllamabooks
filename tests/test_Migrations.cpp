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
    void testForeignKeysEnabled();
    void testFreshSchemaEquivalence();
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

void TestMigrations::testForeignKeysEnabled() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    // Simulate setting PRAGMA foreign_keys = ON; which should be done in BookDatabase::open()
    db.execute("PRAGMA foreign_keys = ON;");

    int enabled = 0;
    QVERIFY(db.queryInt("PRAGMA foreign_keys;", enabled));
    QCOMPARE(enabled, 1);

    db.execute("CREATE TABLE p (id INTEGER PRIMARY KEY);");
    db.execute("CREATE TABLE c (id INTEGER, p_id INTEGER REFERENCES p(id));");

    // Should fail
    QString error;
    QVERIFY(!db.execute("INSERT INTO c (id, p_id) VALUES (1, 1);", &error));
    QVERIFY(error.contains("FOREIGN KEY constraint failed"));

    sqlite3_close(dbHandle);
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

    QVERIFY(checkColumn("documents", "parent_id"));
    QVERIFY(checkColumn("documents", "folder_id"));
    QVERIFY(checkColumn("notes", "folder_id"));
    QVERIFY(checkColumn("messages", "folder_id"));
    QVERIFY(checkColumn("chats", "version"));

    sqlite3_close(dbHandle);
}
