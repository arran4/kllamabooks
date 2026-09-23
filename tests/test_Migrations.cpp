#include <sqlcipher/sqlite3.h>

#include <QSet>
#include <QVector>
#include <QtTest>
#include <cstring>
#include <initializer_list>

#include "../src/BookDatabase.h"
#include "../src/db/Database.h"
#include "../src/db/MigrationFactory.h"
#include "../src/db/Migrations.h"

namespace {

struct ExpectedColumn {
    const char* name;
    const char* type;
    const char* defaultValue;
    int primaryKey;
};

struct ExpectedIndex {
    bool unique;
    std::initializer_list<const char*> columns;
};

struct ExpectedTable {
    const char* name;
    std::initializer_list<ExpectedColumn> columns;
    std::initializer_list<ExpectedIndex> indexes;
};

struct ActualColumn {
    QString name;
    QString type;
    QString defaultValue;
    int primaryKey;
};

struct ActualIndex {
    bool unique;
    QStringList columns;
};

bool readTableColumns(db::Database& db, const QString& table, QVector<ActualColumn>& columns, QString& error) {
    sqlite3_stmt* statement = nullptr;
    const QString sql = QString("PRAGMA table_info(%1);").arg(table);
    if (sqlite3_prepare_v2(db.handle(), sql.toUtf8().constData(), -1, &statement, nullptr) != SQLITE_OK) {
        error = QString::fromUtf8(sqlite3_errmsg(db.handle()));
        return false;
    }

    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        columns.append({QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement, 1))),
                        QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement, 2))),
                        QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement, 4))),
                        sqlite3_column_int(statement, 5)});
    }
    sqlite3_finalize(statement);
    if (result == SQLITE_DONE) {
        return true;
    }

    error = QString::fromUtf8(sqlite3_errmsg(db.handle()));
    return false;
}

bool readTableIndexes(db::Database& db, const QString& table, QVector<ActualIndex>& indexes, QString& error) {
    sqlite3_stmt* indexList = nullptr;
    const QString sql = QString("PRAGMA index_list(%1);").arg(table);
    if (sqlite3_prepare_v2(db.handle(), sql.toUtf8().constData(), -1, &indexList, nullptr) != SQLITE_OK) {
        error = QString::fromUtf8(sqlite3_errmsg(db.handle()));
        return false;
    }

    int result = SQLITE_OK;
    while ((result = sqlite3_step(indexList)) == SQLITE_ROW) {
        const QString indexName = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(indexList, 1)));
        sqlite3_stmt* indexInfo = nullptr;
        const QString indexSql = QString("PRAGMA index_info(%1);").arg(indexName);
        if (sqlite3_prepare_v2(db.handle(), indexSql.toUtf8().constData(), -1, &indexInfo, nullptr) != SQLITE_OK) {
            sqlite3_finalize(indexList);
            error = QString::fromUtf8(sqlite3_errmsg(db.handle()));
            return false;
        }

        QStringList columns;
        int indexResult = SQLITE_OK;
        while ((indexResult = sqlite3_step(indexInfo)) == SQLITE_ROW) {
            columns.append(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(indexInfo, 2))));
        }
        sqlite3_finalize(indexInfo);
        if (indexResult != SQLITE_DONE) {
            sqlite3_finalize(indexList);
            error = QString::fromUtf8(sqlite3_errmsg(db.handle()));
            return false;
        }
        indexes.append({sqlite3_column_int(indexList, 2) != 0, columns});
    }
    sqlite3_finalize(indexList);
    if (result == SQLITE_DONE) {
        return true;
    }

    error = QString::fromUtf8(sqlite3_errmsg(db.handle()));
    return false;
}

bool matchesExpectedSchema(db::Database& db, std::initializer_list<ExpectedTable> expectedTables, QString& error) {
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name;";
    if (sqlite3_prepare_v2(db.handle(), sql, -1, &statement, nullptr) != SQLITE_OK) {
        error = QString::fromUtf8(sqlite3_errmsg(db.handle()));
        return false;
    }

    QSet<QString> actualTables;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        actualTables.insert(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))));
    }
    sqlite3_finalize(statement);

    QSet<QString> expectedTableNames;
    for (const ExpectedTable& expectedTable : expectedTables) {
        expectedTableNames.insert(QString::fromLatin1(expectedTable.name));
    }
    if (actualTables != expectedTableNames) {
        error = QString("unexpected table set: %1").arg(QStringList(actualTables.values()).join(", "));
        return false;
    }

    for (const ExpectedTable& expectedTable : expectedTables) {
        const QString tableName = QString::fromLatin1(expectedTable.name);
        QVector<ActualColumn> actualColumns;
        if (!readTableColumns(db, tableName, actualColumns, error)) {
            return false;
        }
        if (actualColumns.size() != static_cast<int>(expectedTable.columns.size())) {
            error = QString("%1 has %2 columns, expected %3")
                        .arg(tableName)
                        .arg(actualColumns.size())
                        .arg(expectedTable.columns.size());
            return false;
        }

        int columnIndex = 0;
        for (const ExpectedColumn& expectedColumn : expectedTable.columns) {
            const ActualColumn& actualColumn = actualColumns.at(columnIndex++);
            if (actualColumn.name != QLatin1String(expectedColumn.name) ||
                actualColumn.type != QLatin1String(expectedColumn.type) ||
                actualColumn.defaultValue != QLatin1String(expectedColumn.defaultValue) ||
                actualColumn.primaryKey != expectedColumn.primaryKey) {
                error = QString("unexpected %1.%2 definition").arg(tableName, actualColumn.name);
                return false;
            }
        }

        QVector<ActualIndex> actualIndexes;
        if (!readTableIndexes(db, tableName, actualIndexes, error)) {
            return false;
        }
        if (actualIndexes.size() != static_cast<int>(expectedTable.indexes.size())) {
            error = QString("%1 has %2 indexes, expected %3")
                        .arg(tableName)
                        .arg(actualIndexes.size())
                        .arg(expectedTable.indexes.size());
            return false;
        }

        int indexNumber = 0;
        for (const ExpectedIndex& expectedIndex : expectedTable.indexes) {
            const ActualIndex& actualIndex = actualIndexes.at(indexNumber++);
            QStringList expectedColumns;
            for (const char* column : expectedIndex.columns) {
                expectedColumns.append(QString::fromLatin1(column));
            }
            if (actualIndex.unique != expectedIndex.unique || actualIndex.columns != expectedColumns) {
                error = QString("unexpected %1 index definition").arg(tableName);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

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

    db::MigrationRunner runner2;
    runner2.addMigration({0, 1, "test 1", [](db::Database& d) { return false; }});

    QVERIFY(runner2.run(db));

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
    runner2.addMigration({0, 1, "test 1", [](db::Database& d) { return false; }});
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
                             d.execute("CREATE TABLE t1 (id INTEGER);");
                             return false;
                         }});

    QString error;
    QVERIFY(!runner.run(db, &error));

    int count = 0;
    QVERIFY(db.queryInt("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='t1';", count));
    QCOMPARE(count, 0);

    int version = -1;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 0);
    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 0);

    sqlite3_close(dbHandle);
}

void TestMigrations::testForeignKeysEnabled() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    db::Database dbAccess(reinterpret_cast<sqlite3*>(db.getDatabaseHandleForTesting()));

    int enabled = 0;
    QVERIFY(dbAccess.queryInt("PRAGMA foreign_keys;", enabled));
    QCOMPARE(enabled, 1);

    dbAccess.execute("CREATE TABLE p (id INTEGER PRIMARY KEY);");
    dbAccess.execute("CREATE TABLE c (id INTEGER, p_id INTEGER REFERENCES p(id));");

    QString error;
    QVERIFY(!dbAccess.execute("INSERT INTO c (id, p_id) VALUES (1, 1);", &error));
    QVERIFY(error.contains("FOREIGN KEY constraint failed"));

    db.close();
}

void TestMigrations::testFreshSchemaEquivalence() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString error;
    QVERIFY2(runner.run(db, &error), qPrintable(error));

    const std::initializer_list<ExpectedTable> expectedTables = {
        {"schema_version", {{"version", "INTEGER", "", 1}, {"applied_at", "DATETIME", "CURRENT_TIMESTAMP", 0}}, {}},
        {"messages",
         {{"id", "INTEGER", "", 1},
          {"parent_id", "INTEGER", "", 0},
          {"folder_id", "INTEGER", "0", 0},
          {"role", "TEXT", "", 0},
          {"content", "TEXT", "", 0},
          {"timestamp", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"is_expanded", "BOOLEAN", "0", 0}},
         {}},
        {"documents",
         {{"id", "INTEGER", "", 1},
          {"folder_id", "INTEGER", "0", 0},
          {"title", "TEXT", "", 0},
          {"content", "TEXT", "", 0},
          {"timestamp", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"parent_id", "INTEGER", "0", 0},
          {"metadata", "TEXT", "''", 0}},
         {}},
        {"templates",
         {{"id", "INTEGER", "", 1},
          {"folder_id", "INTEGER", "0", 0},
          {"title", "TEXT", "", 0},
          {"content", "TEXT", "", 0},
          {"timestamp", "DATETIME", "CURRENT_TIMESTAMP", 0}},
         {}},
        {"drafts",
         {{"id", "INTEGER", "", 1},
          {"folder_id", "INTEGER", "0", 0},
          {"title", "TEXT", "", 0},
          {"content", "TEXT", "", 0},
          {"timestamp", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"parent_id", "INTEGER", "0", 0},
          {"target_type", "TEXT", "'document'", 0}},
         {}},
        {"notes",
         {{"id", "INTEGER", "", 1},
          {"folder_id", "INTEGER", "0", 0},
          {"title", "TEXT", "", 0},
          {"content", "TEXT", "", 0},
          {"timestamp", "DATETIME", "CURRENT_TIMESTAMP", 0}},
         {}},
        {"folders",
         {{"id", "INTEGER", "", 1},
          {"parent_id", "INTEGER", "0", 0},
          {"name", "TEXT", "", 0},
          {"type", "TEXT", "", 0},
          {"timestamp", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"position", "INTEGER", "0", 0},
          {"is_expanded", "BOOLEAN", "0", 0}},
         {}},
        {"settings",
         {{"scope", "TEXT", "", 1}, {"target_id", "INTEGER", "", 2}, {"key", "TEXT", "", 3}, {"value", "TEXT", "", 0}},
         {{true, {"scope", "target_id", "key"}}}},
        {"queue",
         {{"id", "INTEGER", "", 1},
          {"message_id", "INTEGER", "", 0},
          {"model", "TEXT", "", 0},
          {"prompt", "TEXT", "", 0},
          {"processing_id", "INTEGER", "0", 0},
          {"last_error", "TEXT", "''", 0},
          {"priority", "INTEGER", "0", 0},
          {"created_at", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"target_type", "TEXT", "'message'", 0},
          {"state", "TEXT", "'pending'", 0},
          {"response", "TEXT", "''", 0},
          {"parent_id", "INTEGER", "0", 0},
          {"target_action", "TEXT", "''", 0}},
         {}},
        {"notifications",
         {{"id", "INTEGER", "", 1},
          {"target_id", "INTEGER", "", 0},
          {"type", "TEXT", "", 0},
          {"is_dismissed", "BOOLEAN", "0", 0},
          {"created_at", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"target_type", "TEXT", "'message'", 0}},
         {}},
        {"comments",
         {{"id", "INTEGER", "", 1},
          {"entity_type", "TEXT", "", 0},
          {"entity_id", "INTEGER", "", 0},
          {"content", "TEXT", "", 0},
          {"created_at", "DATETIME", "CURRENT_TIMESTAMP", 0}},
         {}},
        {"chats",
         {{"message_id", "INTEGER", "", 1},
          {"title", "TEXT", "", 0},
          {"systemPrompt", "TEXT", "", 0},
          {"sendBehavior", "TEXT", "", 0},
          {"model", "TEXT", "", 0},
          {"multiLine", "TEXT", "", 0},
          {"draftPrompt", "TEXT", "", 0},
          {"userNote", "TEXT", "", 0},
          {"version", "INTEGER", "0", 0}},
         {}},
        {"document_history",
         {{"id", "INTEGER", "", 1},
          {"document_id", "INTEGER", "", 0},
          {"action_type", "TEXT", "", 0},
          {"content", "TEXT", "", 0},
          {"timestamp", "DATETIME", "CURRENT_TIMESTAMP", 0}},
         {}},
        {"document_merges",
         {{"id", "INTEGER", "", 1},
          {"document_id", "INTEGER", "", 0},
          {"source_document_ids", "TEXT", "", 0},
          {"timestamp", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"version_history_id", "INTEGER", "0", 0}},
         {}},
        {"prompt_history",
         {{"id", "INTEGER", "", 1},
          {"document_id", "INTEGER", "", 0},
          {"prompt", "TEXT", "", 0},
          {"model", "TEXT", "", 0},
          {"timestamp", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"queue_id", "INTEGER", "0", 0}},
         {}},
        {"artifacts",
         {{"id", "INTEGER", "", 1},
          {"kind", "TEXT", "", 0},
          {"folder_id", "INTEGER", "0", 0},
          {"current_version_id", "INTEGER", "0", 0},
          {"created_at", "DATETIME", "CURRENT_TIMESTAMP", 0}},
         {}},
        {"artifact_versions",
         {{"id", "INTEGER", "", 1},
          {"artifact_id", "INTEGER", "", 0},
          {"parent_id", "INTEGER", "0", 0},
          {"title", "TEXT", "", 0},
          {"content", "TEXT", "", 0},
          {"metadata", "TEXT", "''", 0},
          {"is_sealed", "BOOLEAN", "0", 0},
          {"created_at", "DATETIME", "CURRENT_TIMESTAMP", 0}},
         {{true, {"id", "artifact_id"}}}}};
    QVERIFY2(matchesExpectedSchema(db, expectedTables, error), qPrintable(error));

    int version = 0;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 22);
    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 22);

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
    runner.addMigration({2, 3, "test alter", [](db::Database& d) {
                             d.execute("CREATE TABLE t1 (id INTEGER);");
                             return d.execute("ALTER TABLE documents ADD COLUMN;");
                         }});

    QString error;
    QVERIFY(!runner.run(db, &error));

    int count = 0;
    QVERIFY(db.queryInt("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='t1';", count));
    QCOMPARE(count, 0);

    int version = -1;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 2);
    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version;", version));
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
    db.execute("PRAGMA user_version = 3;");

    db::MigrationRunner runner;
    bool migrationExecuted = false;
    runner.addMigration({3, 4, "test", [&migrationExecuted](db::Database& d) {
                             migrationExecuted = true;
                             return d.execute("CREATE TABLE t1 (id INTEGER);");
                         }});

    QString error;
    QVERIFY(!runner.run(db, &error));
    QVERIFY(error.contains("Version disagreement"));
    QVERIFY(!migrationExecuted);

    int version = -1;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 3);
    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 2);

    sqlite3* dbHandle2;
    sqlite3_open(":memory:", &dbHandle2);
    db::Database db2(dbHandle2);
    db2.execute(
        "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    db2.execute("PRAGMA user_version = 2;");

    db::MigrationRunner runner2;
    bool secondMigrationExecuted = false;
    runner2.addMigration({2, 3, "test", [&secondMigrationExecuted](db::Database& d) {
                              secondMigrationExecuted = true;
                              return d.execute("CREATE TABLE t2 (id INTEGER);");
                          }});
    QVERIFY(!runner2.run(db2, &error));
    QVERIFY(error.contains("Version disagreement"));

    int count = 0;
    QVERIFY(db2.queryInt("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='t2';", count));
    QCOMPARE(count, 0);
    QVERIFY(!secondMigrationExecuted);

    version = -1;
    QVERIFY(db2.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 2);

    version = -1;
    QVERIFY(db2.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 0);

    sqlite3_close(dbHandle2);

    sqlite3* dbHandle3;
    sqlite3_open(":memory:", &dbHandle3);
    db::Database db3(dbHandle3);
    db3.execute(
        "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    db3.execute("INSERT INTO schema_version (version) VALUES (3);");
    db3.execute("PRAGMA user_version = 0;");
    QVERIFY(!runner.run(db3, &error));
    QVERIFY(error.contains("Version disagreement"));
    QVERIFY(db3.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 0);
    QVERIFY(db3.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 3);

    sqlite3* dbHandle4;
    sqlite3_open(":memory:", &dbHandle4);
    db::Database db4(dbHandle4);
    db4.execute(
        "CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    db4.execute("INSERT INTO schema_version (version) VALUES (2);");
    db4.execute("PRAGMA user_version = 2;");

    db::MigrationRunner metadataRunner;
    bool metadataMigrationExecuted = false;
    metadataRunner.addMigration({2, 3, "test", [&metadataMigrationExecuted](db::Database& d) {
                                     metadataMigrationExecuted = true;
                                     return d.execute("CREATE TABLE t3 (id INTEGER);");
                                 }});
    sqlite3_set_authorizer(
        dbHandle4,
        [](void*, int action, const char* table, const char* column, const char*, const char*) {
            if (action == SQLITE_READ && table && column && std::strcmp(table, "schema_version") == 0 &&
                std::strcmp(column, "version") == 0) {
                return SQLITE_DENY;
            }
            return SQLITE_OK;
        },
        nullptr);
    QVERIFY(!metadataRunner.run(db4, &error));
    QVERIFY(error.contains("schema_version"));
    QVERIFY(!metadataMigrationExecuted);
    QVERIFY(db4.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 2);

    sqlite3_set_authorizer(dbHandle4, nullptr, nullptr);
    QVERIFY(db4.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 2);

    sqlite3_close(dbHandle3);
    sqlite3_close(dbHandle4);
    sqlite3_close(dbHandle);
}

void TestMigrations::testLegacySeeding() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db.execute("PRAGMA user_version = 2;");

    db::MigrationRunner runner;
    runner.addMigration({2, 3, "test", [](db::Database& d) { return d.execute("CREATE TABLE t1 (id INTEGER);"); }});

    QString error;
    QVERIFY(runner.run(db, &error));

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

    db.execute("PRAGMA user_version = 2;");

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
    QVERIFY(!error.isEmpty());

    sqlite3_set_authorizer(dbHandle, nullptr, nullptr);

    int count = 0;
    QVERIFY(db.queryInt("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='schema_version';", count));
    QCOMPARE(count, 0);

    int version = -1;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 2);

    QVERIFY(runner.run(db, &error));
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 3);
    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 3);

    sqlite3_close(dbHandle);
}

QTEST_MAIN(TestMigrations)
#include "test_Migrations.moc"
