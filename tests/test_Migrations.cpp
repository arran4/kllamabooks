#include <sqlcipher/sqlite3.h>

#include <QFile>
#include <QSet>
#include <QTemporaryDir>
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
    void testDocumentMigrationIdempotency();
    void testDocumentMigrationFailure();
    void testDocumentMigrationIntegrity();
    void testDocumentMigrationPreservation();
    void testDocumentMigrationIterationFailure();
    void testMigration24To25();
    void testEncryptedFileBackedHistoricalFixtureUpgradeToV25();
    void testMigration26To27HistoryCatchupAndQuarantine();
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
          {"is_expanded", "BOOLEAN", "0", 0},
          {"chat_id", "INTEGER", "0", 0},
          {"run_id", "INTEGER", "0", 0}},
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
         {{false, {"parent_id", "type"}}}},
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
          {"target_type", "TEXT", "'message'", 0},
          {"run_id", "INTEGER", "0", 0},
          {"severity", "TEXT", "'info'", 0},
          {"title", "TEXT", "''", 0},
          {"message", "TEXT", "''", 0},
          {"dismissed_at", "DATETIME", "NULL", 0}},
         {{false, {"is_dismissed", "created_at"}}}},
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
        {"legacy_document_mapping",
         {{"document_id", "INTEGER", "", 1}, {"artifact_id", "INTEGER", "", 0}},
         {{true, {"artifact_id"}}}},
        {"legacy_note_mapping",
         {{"note_id", "INTEGER", "", 1}, {"artifact_id", "INTEGER", "", 0}},
         {{true, {"artifact_id"}}}},
        {"legacy_template_mapping",
         {{"template_id", "INTEGER", "", 1}, {"artifact_id", "INTEGER", "", 0}},
         {{true, {"artifact_id"}}}},
        {"legacy_draft_mapping",
         {{"draft_id", "INTEGER", "", 1},
          {"artifact_id", "INTEGER", "", 0},
          {"legacy_parent_id", "INTEGER", "", 0},
          {"legacy_target_type", "TEXT", "", 0}},
         {{true, {"artifact_id"}}}},
        {"legacy_document_history_mapping",
         {{"history_id", "INTEGER", "", 1}, {"artifact_version_id", "INTEGER", "", 0}},
         {{true, {"artifact_version_id"}}}},
        {"artifact_versions",
         {{"id", "INTEGER", "", 1},
          {"artifact_id", "INTEGER", "", 0},
          {"parent_id", "INTEGER", "", 0},
          {"forked_from_version_id", "INTEGER", "", 0},
          {"title", "TEXT", "", 0},
          {"content", "TEXT", "", 0},
          {"metadata", "TEXT", "''", 0},
          {"is_sealed", "BOOLEAN", "0", 0},
          {"created_at", "DATETIME", "CURRENT_TIMESTAMP", 0}},
         {{true, {"id", "artifact_id"}}}},
        {"quarantined_records",
         {{"id", "INTEGER", "", 1},
          {"source_table", "TEXT", "", 0},
          {"legacy_id", "INTEGER", "", 0},
          {"target_type", "TEXT", "''", 0},
          {"parent_reference", "INTEGER", "", 0},
          {"title", "TEXT", "", 0},
          {"content", "TEXT", "", 0},
          {"raw_payload_json", "TEXT", "", 0},
          {"reason", "TEXT", "", 0},
          {"status", "TEXT", "'quarantined'", 0},
          {"resolved_target_kind", "TEXT", "", 0},
          {"resolved_target_id", "INTEGER", "", 0},
          {"created_at", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"resolved_at", "DATETIME", "", 0}},
         {{false, {"source_table", "legacy_id"}}, {false, {"status"}}}},
        {"prompt_revisions",
         {{"id", "INTEGER", "", 1},
          {"source_kind", "TEXT", "'user'", 0},
          {"template_id", "TEXT", "''", 0},
          {"target_kind", "TEXT", "''", 0},
          {"target_id", "INTEGER", "0", 0},
          {"system_prompt", "TEXT", "''", 0},
          {"prompt_text", "TEXT", "", 0},
          {"model", "TEXT", "''", 0},
          {"is_sealed", "BOOLEAN", "0", 0},
          {"created_at", "DATETIME", "CURRENT_TIMESTAMP", 0}},
         {}},
        {"ai_runs",
         {{"id", "INTEGER", "", 1},
          {"parent_run_id", "INTEGER", "NULL", 0},
          {"prompt_revision_id", "INTEGER", "", 0},
          {"target_kind", "TEXT", "'message'", 0},
          {"target_id", "INTEGER", "0", 0},
          {"base_version_id", "INTEGER", "0", 0},
          {"target_action", "TEXT", "''", 0},
          {"model", "TEXT", "''", 0},
          {"state", "TEXT", "'pending'", 0},
          {"priority", "INTEGER", "0", 0},
          {"worker_instance_id", "TEXT", "''", 0},
          {"lease_expires_at", "DATETIME", "NULL", 0},
          {"response_text", "TEXT", "''", 0},
          {"result_version_id", "INTEGER", "0", 0},
          {"last_error", "TEXT", "''", 0},
          {"created_at", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"started_at", "DATETIME", "NULL", 0},
          {"finished_at", "DATETIME", "NULL", 0}},
         {{false, {"state", "priority", "created_at"}}}},
        {"ai_run_inputs",
         {{"id", "INTEGER", "", 1},
          {"run_id", "INTEGER", "", 0},
          {"ordinal", "INTEGER", "", 0},
          {"artifact_id", "INTEGER", "0", 0},
          {"version_id", "INTEGER", "0", 0},
          {"role", "TEXT", "'source'", 0}},
         {{true, {"run_id", "ordinal"}}}},
        {"chat_sessions",
         {{"id", "INTEGER", "", 1},
          {"root_message_id", "INTEGER", "0", 0},
          {"folder_id", "INTEGER", "0", 0},
          {"title", "TEXT", "''", 0},
          {"system_prompt", "TEXT", "''", 0},
          {"model", "TEXT", "''", 0},
          {"created_at", "DATETIME", "CURRENT_TIMESTAMP", 0},
          {"updated_at", "DATETIME", "CURRENT_TIMESTAMP", 0}},
         {{false, {"root_message_id"}}}}};
    QVERIFY2(matchesExpectedSchema(db, expectedTables, error), qPrintable(error));

    int version = 0;
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 31);
    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 31);

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

    QVERIFY2(runner.run(db, &error), qPrintable(error));
    QVERIFY(db.queryInt("PRAGMA user_version;", version));
    QCOMPARE(version, 3);
    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version;", version));
    QCOMPARE(version, 3);

    sqlite3_close(dbHandle);
}

void TestMigrations::testDocumentMigrationIdempotency() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    // we only run up to version 22
    db::MigrationRunner runner22;
    for (const auto& m : runner.getMigrations()) {
        if (m.toVersion <= 22) {
            runner22.addMigration(m);
        }
    }

    QString error;
    QVERIFY2(runner22.run(db, &error), qPrintable(error));

    // Insert some legacy documents
    db.execute(
        "INSERT INTO documents (folder_id, title, content, metadata) VALUES (1, 'Doc 1', 'Content 1', 'Meta 1');");
    db.execute(
        "INSERT INTO documents (folder_id, title, content, metadata) VALUES (2, 'Doc 2', 'Content 2', 'Meta 2');");

    // Run migration 22->23
    db::MigrationRunner runner23;
    for (const auto& m : runner.getMigrations()) {
        if (m.toVersion == 23) {
            runner23.addMigration(m);
        }
    }

    QVERIFY2(runner23.run(db, &error), qPrintable(error));

    // Verify artifacts created
    int count = 0;
    db.queryInt("SELECT COUNT(*) FROM artifacts", count);
    QCOMPARE(count, 2);
    db.queryInt("SELECT COUNT(*) FROM artifact_versions", count);
    QCOMPARE(count, 2);
    db.queryInt("SELECT COUNT(*) FROM legacy_document_mapping", count);
    QCOMPARE(count, 2);

    // Add another document (simulating pre-v23 activity in legacy UI)
    db.execute(
        "INSERT INTO documents (folder_id, title, content, metadata) VALUES (1, 'Doc 3', 'Content 3', 'Meta 3');");

    // Re-run migration 23 (should be no-op for existing, but mapping handles it if we run it directly)
    // Actually runner.run will skip 23 if user_version is 23. Let's decrement user_version to test idempotency of the
    // migration logic itself
    db.execute("PRAGMA user_version = 22;");
    db.execute("DELETE FROM schema_version WHERE version > 22;");
    QVERIFY2(runner23.run(db, &error), qPrintable(error));

    db.queryInt("SELECT COUNT(*) FROM artifacts", count);
    QCOMPARE(count, 3);
    db.queryInt("SELECT COUNT(*) FROM legacy_document_mapping", count);
    QCOMPARE(count, 3);

    sqlite3_close(dbHandle);
}

void TestMigrations::testDocumentMigrationFailure() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    db::MigrationRunner runner22;
    for (const auto& m : runner.getMigrations()) {
        if (m.toVersion <= 22) {
            runner22.addMigration(m);
        }
    }

    QString error;
    QVERIFY(runner22.run(db, &error));
    db.execute(
        "INSERT INTO documents (folder_id, title, content, metadata) VALUES (1, 'Doc 1', 'Content 1', 'Meta 1');");

    // Deliberately cause a failure after at least one migration write
    // by using an aborting trigger on artifact_versions insert
    db.execute(
        "CREATE TRIGGER fail_artifact_version BEFORE INSERT ON artifact_versions BEGIN SELECT RAISE(ABORT, "
        "'intentional failure'); END;");

    db::MigrationRunner runner23;
    for (const auto& m : runner.getMigrations()) {
        if (m.toVersion == 23) {
            runner23.addMigration(m);
        }
    }

    QVERIFY(!runner23.run(db, &error));

    // Verify rollback
    int count = -1;
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM artifacts", count));
    QCOMPARE(count, 0);  // Rollback should have removed the artifact
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM artifact_versions", count));
    QCOMPARE(count, 0);
    QVERIFY(!db.queryInt("SELECT COUNT(*) FROM legacy_document_mapping", count));  // table shouldn't exist

    int version = 0;
    QVERIFY(db.queryInt("PRAGMA user_version", version));
    QCOMPARE(version, 22);
    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version", version));
    QCOMPARE(version, 22);

    QVERIFY(db.queryInt("SELECT COUNT(*) FROM documents", count));
    QCOMPARE(count, 1);

    db.execute("DROP TRIGGER fail_artifact_version;");

    sqlite3_close(dbHandle);
}

void TestMigrations::testDocumentMigrationIntegrity() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString error;
    QVERIFY(runner.run(db, &error));

    // test foreign key check
    db.execute("PRAGMA foreign_keys = ON;");
    int issues = 0;
    db.queryInt("SELECT COUNT(*) FROM pragma_foreign_key_check()", issues);
    QCOMPARE(issues, 0);

    // test integrity check
    int isOk = 0;
    db.queryInt("SELECT COUNT(*) FROM pragma_integrity_check() WHERE pragma_integrity_check != 'ok'", isOk);
    QCOMPARE(isOk, 0);

    sqlite3_close(dbHandle);
}

void TestMigrations::testDocumentMigrationPreservation() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    db::MigrationRunner runner22;
    for (const auto& m : runner.getMigrations()) {
        if (m.toVersion <= 22) {
            runner22.addMigration(m);
        }
    }

    QString error;
    QVERIFY(runner22.run(db, &error));

    // insert a complex document
    db.execute(
        "INSERT INTO documents (id, folder_id, title, content, metadata, timestamp) "
        "VALUES (100, 5, 'Special Title', 'Line 1\nLine 2', '{\"key\": \"value\"}', '2023-01-01 12:00:00');");

    db::MigrationRunner runner23;
    for (const auto& m : runner.getMigrations()) {
        if (m.toVersion == 23) {
            runner23.addMigration(m);
        }
    }

    QVERIFY(runner23.run(db, &error));

    sqlite3_stmt* stmt = nullptr;
    QCOMPARE(
        sqlite3_prepare_v2(dbHandle,
                           "SELECT a.kind, a.folder_id, v.title, v.content, v.metadata, v.created_at, m.document_id "
                           "FROM artifacts a JOIN artifact_versions v ON a.current_version_id = v.id "
                           "JOIN legacy_document_mapping m ON m.artifact_id = a.id WHERE m.document_id = 100;",
                           -1, &stmt, nullptr),
        SQLITE_OK);

    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(QString((const char*)sqlite3_column_text(stmt, 0)), QString("document"));
    QCOMPARE(sqlite3_column_int(stmt, 1), 5);
    QCOMPARE(QString((const char*)sqlite3_column_text(stmt, 2)), QString("Special Title"));
    QCOMPARE(QString((const char*)sqlite3_column_text(stmt, 3)), QString("Line 1\nLine 2"));
    QCOMPARE(QString((const char*)sqlite3_column_text(stmt, 4)), QString("{\"key\": \"value\"}"));
    QCOMPARE(QString((const char*)sqlite3_column_text(stmt, 5)), QString("2023-01-01 12:00:00"));
    QCOMPARE(sqlite3_column_int(stmt, 6), 100);

    sqlite3_finalize(stmt);
    sqlite3_close(dbHandle);
}

void TestMigrations::testDocumentMigrationIterationFailure() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    db::MigrationRunner runner22;
    for (const auto& m : runner.getMigrations()) {
        if (m.toVersion <= 22) {
            runner22.addMigration(m);
        }
    }

    QString error;
    QVERIFY(runner22.run(db, &error));
    db.execute(
        "INSERT INTO documents (folder_id, title, content, metadata) VALUES (1, 'Doc 1', 'Content 1', 'Meta 1');");

    // Re-create the documents table as a view over documents_real to inject a failing user-defined function
    // during the `sqlite3_step` loop for migration 23.
    QVERIFY(db.execute("ALTER TABLE documents RENAME TO documents_real;"));

    bool functionCalled = false;
    QCOMPARE(sqlite3_create_function(
                 dbHandle, "trigger_step_error", 0, SQLITE_UTF8, &functionCalled,
                 [](sqlite3_context* context, int, sqlite3_value**) {
                     bool* called = static_cast<bool*>(sqlite3_user_data(context));
                     *called = true;
                     sqlite3_result_error(context, "injected iteration error", -1);
                 },
                 nullptr, nullptr),
             SQLITE_OK);

    QVERIFY(
        db.execute("CREATE VIEW documents AS SELECT id, folder_id, title, trigger_step_error() as content, timestamp, "
                   "metadata FROM documents_real;"));

    db::MigrationRunner runner23;
    for (const auto& m : runner.getMigrations()) {
        if (m.toVersion == 23) {
            runner23.addMigration(m);
        }
    }

    QVERIFY(!runner23.run(db, &error));
    QVERIFY(functionCalled);

    // Verify rollback
    int count = -1;
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM artifacts", count));
    QCOMPARE(count, 0);

    int version = 0;
    QVERIFY(db.queryInt("PRAGMA user_version", version));
    QCOMPARE(version, 22);
    QVERIFY(db.queryInt("SELECT MAX(version) FROM schema_version", version));
    QCOMPARE(version, 22);

    sqlite3_close(dbHandle);
}

void TestMigrations::testMigration24To25() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    db::MigrationRunner runner24;
    for (const auto& m : runner.getMigrations()) {
        if (m.toVersion <= 24) {
            runner24.addMigration(m);
        }
    }

    QString error;
    QVERIFY(runner24.run(db, &error));

    // Insert legacy data
    QVERIFY(
        db.execute("INSERT INTO documents (folder_id, title, content, metadata, timestamp) VALUES (1, 'Doc 1', "
                   "'Content 1', 'Meta 1', '2025-01-01 10:00:00');"));
    QVERIFY(db.execute(
        "INSERT INTO documents (folder_id, title, content, metadata, timestamp) VALUES (1, 'Doc Mapped', 'Content "
        "Mapped', 'Meta 2', '2025-01-01 10:00:01');"));

    // We let the DB auto-increment ID to avoid any constraints we missed.
    QVERIFY(
        db.execute("INSERT INTO artifacts (kind, folder_id, created_at) VALUES ('document', 1, CURRENT_TIMESTAMP);"));
    int artId = 0;
    QVERIFY(db.queryInt("SELECT MAX(id) FROM artifacts;", artId));

    QVERIFY(db.execute(QString("INSERT INTO artifact_versions (artifact_id, title, content, metadata, created_at) "
                               "VALUES (%1, 'Doc Mapped', 'Content Mapped', 'Meta 2', CURRENT_TIMESTAMP);")
                           .arg(artId)));
    int verId = 0;
    QVERIFY(db.queryInt("SELECT MAX(id) FROM artifact_versions;", verId));

    QVERIFY(db.execute(QString("UPDATE artifacts SET current_version_id = %1 WHERE id = %2;").arg(verId).arg(artId)));
    QVERIFY(db.execute(
        QString("INSERT INTO legacy_document_mapping (document_id, artifact_id) VALUES (2, %1);").arg(artId)));

    QVERIFY(
        db.execute("INSERT INTO notes (folder_id, title, content, timestamp) VALUES (2, 'Note 1', 'Content Note 1', "
                   "'2025-01-01 10:00:02');"));
    // Insert a NULL title and content note to prove exact preservation of NULLs
    QVERIFY(db.execute("INSERT INTO notes (folder_id, timestamp) VALUES (10, '2025-01-01 10:00:09');"));
    // Insert a totally NULL timestamp note
    QVERIFY(db.execute("INSERT INTO notes (folder_id, title, timestamp) VALUES (11, 'Null Timestamp Note', NULL);"));

    QVERIFY(
        db.execute("INSERT INTO templates (folder_id, title, content, timestamp) VALUES (5, 'Template 1', 'Content "
                   "Template 1', '2025-01-01 10:00:03');"));

    // Draft that is a child of the unmapped doc 1
    QVERIFY(db.execute(
        "INSERT INTO drafts (folder_id, title, content, parent_id, target_type, timestamp) VALUES (3, 'Draft 1', "
        "'Content Draft 1', 1, 'document', '2025-01-01 10:00:04');"));

    // Draft with NULL parent_id and target_type
    QVERIFY(db.execute(
        "INSERT INTO drafts (folder_id, title, content, parent_id, target_type, timestamp) VALUES (4, 'Draft 2', "
        "'Content Draft 2', NULL, NULL, '2025-01-01 10:00:05');"));

    // Draft that is a child of note 1 (from migration, its local ID is 1, so ID 1)
    QVERIFY(db.execute(
        "INSERT INTO drafts (folder_id, title, content, parent_id, target_type, timestamp) VALUES (5, 'Draft 3', "
        "'Content Draft 3', 1, 'note', '2025-01-01 10:00:06');"));

    // Draft that is a child of template 1
    QVERIFY(db.execute(
        "INSERT INTO drafts (folder_id, title, content, parent_id, target_type, timestamp) VALUES (6, 'Draft 4', "
        "'Content Draft 4', 1, 'template', '2025-01-01 10:00:07');"));

    // Draft that is a child of an unknown type
    QVERIFY(db.execute(
        "INSERT INTO drafts (folder_id, title, content, parent_id, target_type, timestamp) VALUES (7, 'Draft 5', "
        "'Content Draft 5', 1, 'unknown_type', '2025-01-01 10:00:08');"));

    // Draft that is a child of another draft (draft 1, which has ID 1 locally)
    QVERIFY(db.execute(
        "INSERT INTO drafts (folder_id, title, content, parent_id, target_type, timestamp) VALUES (8, 'Draft 6', "
        "'Content Draft 6', 1, 'draft', '2025-01-01 10:00:09');"));

    // Run migration 24->25
    db::MigrationRunner runner25;
    for (const auto& m : runner.getMigrations()) {
        if (m.toVersion == 25) {
            runner25.addMigration(m);
        }
    }

    QVERIFY(runner25.run(db, &error));

    // Assert mappings exist
    int count = 0;
    db.queryInt("SELECT COUNT(*) FROM artifacts", count);
    QCOMPARE(count, 12);
    db.queryInt("SELECT COUNT(*) FROM legacy_document_mapping", count);
    QCOMPARE(count, 2);
    db.queryInt("SELECT COUNT(*) FROM legacy_note_mapping", count);
    QCOMPARE(count, 3);
    db.queryInt("SELECT COUNT(*) FROM legacy_template_mapping", count);
    QCOMPARE(count, 1);
    db.queryInt("SELECT COUNT(*) FROM legacy_draft_mapping", count);
    QCOMPARE(count, 6);

    // Verify artifact kinds
    db.queryInt("SELECT COUNT(*) FROM artifacts WHERE kind = 'document'", count);
    QCOMPARE(count, 2);
    db.queryInt("SELECT COUNT(*) FROM artifacts WHERE kind = 'note'", count);
    QCOMPARE(count, 3);
    db.queryInt("SELECT COUNT(*) FROM artifacts WHERE kind = 'template'", count);
    QCOMPARE(count, 1);
    db.queryInt("SELECT COUNT(*) FROM artifacts WHERE kind = 'draft'", count);
    QCOMPARE(count, 6);

    // Detailed Preservation checks
    sqlite3_stmt* stmt = nullptr;

    // Document
    QCOMPARE(sqlite3_prepare_v2(db.handle(),
                                "SELECT a.folder_id, v.title, v.content, v.metadata, v.created_at FROM artifacts a "
                                "JOIN artifact_versions v ON a.current_version_id = v.id JOIN legacy_document_mapping "
                                "m ON a.id = m.artifact_id WHERE m.document_id = 1",
                                -1, &stmt, nullptr),
             SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(sqlite3_column_int(stmt, 0), 1);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))), QString("Doc 1"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))), QString("Content 1"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))), QString("Meta 1"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))),
             QString("2025-01-01 10:00:00"));
    sqlite3_finalize(stmt);

    // Note
    QCOMPARE(
        sqlite3_prepare_v2(
            db.handle(),
            "SELECT a.folder_id, v.title, v.content, v.metadata, v.created_at FROM artifacts a JOIN artifact_versions "
            "v ON a.current_version_id = v.id JOIN legacy_note_mapping m ON a.id = m.artifact_id WHERE m.note_id = 1",
            -1, &stmt, nullptr),
        SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(sqlite3_column_int(stmt, 0), 2);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))), QString("Note 1"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))), QString("Content Note 1"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))), QString(""));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))),
             QString("2025-01-01 10:00:02"));
    sqlite3_finalize(stmt);

    // Template
    QCOMPARE(sqlite3_prepare_v2(db.handle(),
                                "SELECT a.folder_id, v.title, v.content, v.metadata, v.created_at FROM artifacts a "
                                "JOIN artifact_versions v ON a.current_version_id = v.id JOIN legacy_template_mapping "
                                "m ON a.id = m.artifact_id WHERE m.template_id = 1",
                                -1, &stmt, nullptr),
             SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(sqlite3_column_int(stmt, 0), 5);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))), QString("Template 1"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))),
             QString("Content Template 1"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))), QString(""));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))),
             QString("2025-01-01 10:00:03"));
    sqlite3_finalize(stmt);

    // Draft
    QCOMPARE(
        sqlite3_prepare_v2(db.handle(),
                           "SELECT a.folder_id, v.title, v.content, v.metadata, v.created_at, m.legacy_parent_id, "
                           "m.legacy_target_type FROM artifacts a JOIN artifact_versions v ON a.current_version_id = "
                           "v.id JOIN legacy_draft_mapping m ON a.id = m.artifact_id WHERE m.draft_id = 1",
                           -1, &stmt, nullptr),
        SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(sqlite3_column_int(stmt, 0), 3);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))), QString("Draft 1"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))),
             QString("Content Draft 1"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))), QString(""));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))),
             QString("2025-01-01 10:00:04"));
    QCOMPARE(sqlite3_column_int(stmt, 5), 1);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6))), QString("document"));
    sqlite3_finalize(stmt);

    // Verify template folder preservation
    int folderId = 0;
    db.queryInt("SELECT folder_id FROM artifacts WHERE kind = 'template'", folderId);
    QCOMPARE(folderId, 5);

    // Draft 1 should have forked_from_version_id pointing to the document's version
    int expectedDocumentVersionId = 0;
    QVERIFY(
        db.queryInt("SELECT a.current_version_id FROM legacy_document_mapping m JOIN artifacts a ON a.id = "
                    "m.artifact_id WHERE m.document_id = 1;",
                    expectedDocumentVersionId));
    QVERIFY(expectedDocumentVersionId > 0);
    int forkedFromId = 0;
    QVERIFY(
        db.queryInt("SELECT forked_from_version_id FROM artifact_versions v JOIN legacy_draft_mapping m ON "
                    "v.artifact_id = m.artifact_id WHERE m.draft_id = 1",
                    forkedFromId));
    QCOMPARE(forkedFromId, expectedDocumentVersionId);

    // Draft 3 (ID 3 in DB) should have forked_from_version_id pointing to the note's version
    int expectedNoteVersionId = 0;
    QVERIFY(
        db.queryInt("SELECT a.current_version_id FROM legacy_note_mapping m JOIN artifacts a ON a.id = "
                    "m.artifact_id WHERE m.note_id = 1;",
                    expectedNoteVersionId));
    QVERIFY(expectedNoteVersionId > 0);
    int forkedFromIdNote = 0;
    QVERIFY(
        db.queryInt("SELECT forked_from_version_id FROM artifact_versions v JOIN legacy_draft_mapping m ON "
                    "v.artifact_id = m.artifact_id WHERE m.draft_id = 3",
                    forkedFromIdNote));
    QCOMPARE(forkedFromIdNote, expectedNoteVersionId);

    // Draft 4 (ID 4 in DB) should have forked_from_version_id pointing to the template's version
    int expectedTemplateVersionId = 0;
    QVERIFY(
        db.queryInt("SELECT a.current_version_id FROM legacy_template_mapping m JOIN artifacts a ON a.id = "
                    "m.artifact_id WHERE m.template_id = 1;",
                    expectedTemplateVersionId));
    QVERIFY(expectedTemplateVersionId > 0);
    int forkedFromIdTemplate = 0;
    QVERIFY(
        db.queryInt("SELECT forked_from_version_id FROM artifact_versions v JOIN legacy_draft_mapping m ON "
                    "v.artifact_id = m.artifact_id WHERE m.draft_id = 4",
                    forkedFromIdTemplate));
    QCOMPARE(forkedFromIdTemplate, expectedTemplateVersionId);

    // Draft 2 (ID 2 in DB) should have NULLs
    QCOMPARE(
        sqlite3_prepare_v2(db.handle(),
                           "SELECT legacy_parent_id, legacy_target_type FROM legacy_draft_mapping WHERE draft_id = 2",
                           -1, &stmt, nullptr),
        SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(sqlite3_column_type(stmt, 0) == SQLITE_NULL, true);
    QCOMPARE(sqlite3_column_type(stmt, 1) == SQLITE_NULL, true);
    sqlite3_finalize(stmt);

    int forkedFromIdNull = 0;
    QCOMPARE(db.queryInt("SELECT forked_from_version_id FROM artifact_versions v JOIN legacy_draft_mapping m ON "
                         "v.artifact_id = m.artifact_id WHERE m.draft_id = 2 AND forked_from_version_id IS NOT NULL",
                         forkedFromIdNull),
             false);  // No row returned for NOT NULL => it IS NULL

    // Draft 5 (ID 5 in DB) unknown type should have NULLs for forked_from_version_id but keeps raw
    QCOMPARE(
        sqlite3_prepare_v2(db.handle(),
                           "SELECT legacy_parent_id, legacy_target_type FROM legacy_draft_mapping WHERE draft_id = 5",
                           -1, &stmt, nullptr),
        SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(sqlite3_column_int(stmt, 0), 1);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))), QString("unknown_type"));
    sqlite3_finalize(stmt);
    QCOMPARE(db.queryInt("SELECT forked_from_version_id FROM artifact_versions v JOIN legacy_draft_mapping m ON "
                         "v.artifact_id = m.artifact_id WHERE m.draft_id = 5 AND forked_from_version_id IS NOT NULL",
                         forkedFromIdNull),
             false);  // No row returned for NOT NULL => it IS NULL

    // Verify mapped document remains the same and is not duplicated
    int mappedArtId = 0;
    QVERIFY(db.queryInt("SELECT artifact_id FROM legacy_document_mapping WHERE document_id = 2", mappedArtId));
    QCOMPARE(mappedArtId, artId);

    // Check Draft 6 lineage (draft -> draft)
    int expectedDraftVersionId = 0;
    QVERIFY(
        db.queryInt("SELECT a.current_version_id FROM legacy_draft_mapping m JOIN artifacts a ON a.id = "
                    "m.artifact_id WHERE m.draft_id = 1;",
                    expectedDraftVersionId));
    QVERIFY(expectedDraftVersionId > 0);
    int forkedFromIdDraft = 0;
    QVERIFY(
        db.queryInt("SELECT forked_from_version_id FROM artifact_versions v JOIN legacy_draft_mapping m ON "
                    "v.artifact_id = m.artifact_id WHERE m.draft_id = 6",
                    forkedFromIdDraft));
    QCOMPARE(forkedFromIdDraft, expectedDraftVersionId);

    // Verify NULL note preservation
    QCOMPARE(
        sqlite3_prepare_v2(
            db.handle(),
            "SELECT a.folder_id, v.title, v.content, v.metadata, v.created_at FROM artifacts a JOIN artifact_versions "
            "v ON a.current_version_id = v.id JOIN legacy_note_mapping m ON a.id = m.artifact_id WHERE m.note_id = 2",
            -1, &stmt, nullptr),
        SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(sqlite3_column_int(stmt, 0), 10);
    QCOMPARE(sqlite3_column_type(stmt, 1) == SQLITE_NULL, true);
    QCOMPARE(sqlite3_column_type(stmt, 2) == SQLITE_NULL, true);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))), QString(""));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))),
             QString("2025-01-01 10:00:09"));
    sqlite3_finalize(stmt);

    // Verify completely NULL timestamp note
    QCOMPARE(
        sqlite3_prepare_v2(
            db.handle(),
            "SELECT a.created_at, v.created_at FROM artifacts a JOIN artifact_versions "
            "v ON a.current_version_id = v.id JOIN legacy_note_mapping m ON a.id = m.artifact_id WHERE m.note_id = 3",
            -1, &stmt, nullptr),
        SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(sqlite3_column_type(stmt, 0) == SQLITE_NULL, true);
    QCOMPARE(sqlite3_column_type(stmt, 1) == SQLITE_NULL, true);
    sqlite3_finalize(stmt);

    // Assert sealing logic:
    int sealedCount = 0;
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM artifact_versions WHERE is_sealed = 1", sealedCount));
    QCOMPARE(sealedCount, 4);  // The 4 sources should be sealed

    int unsealedCount = 0;
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM artifact_versions WHERE is_sealed = 0", unsealedCount));
    QCOMPARE(unsealedCount, 8);  // Total 12 versions. 12 - 4 = 8 unsealed.

    // PRAGMA checks
    int pragmaCount = 0;
    QCOMPARE(sqlite3_prepare_v2(db.handle(), "PRAGMA foreign_key_check;", -1, &stmt, nullptr), SQLITE_OK);
    while (sqlite3_step(stmt) == SQLITE_ROW) pragmaCount++;
    sqlite3_finalize(stmt);
    QCOMPARE(pragmaCount, 0);

    QString integrity;
    QCOMPARE(sqlite3_prepare_v2(db.handle(), "PRAGMA integrity_check;", -1, &stmt, nullptr), SQLITE_OK);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        integrity = (const char*)sqlite3_column_text(stmt, 0);
    }
    sqlite3_finalize(stmt);
    QCOMPARE(integrity, QString("ok"));

    // Verify idempotency
    QVERIFY(runner25.run(db, &error));
    db.queryInt("SELECT COUNT(*) FROM artifacts", count);
    QCOMPARE(count, 12);

    // Inject migration failure AFTER some rows are created
    sqlite3* dbHandleFail;
    sqlite3_open(":memory:", &dbHandleFail);
    db::Database dbFail(dbHandleFail);
    QVERIFY(runner24.run(dbFail, &error));

    // Seed some documents and notes
    dbFail.execute(
        "INSERT INTO documents (folder_id, title, content, metadata) VALUES (1, 'Doc 1', 'Content 1', 'Meta 1');");
    dbFail.execute("INSERT INTO notes (folder_id, title, content) VALUES (2, 'Note 1', 'Content Note 1');");

    int artifactsBefore = 0;
    dbFail.queryInt("SELECT COUNT(*) FROM artifacts", artifactsBefore);
    QCOMPARE(artifactsBefore, 0);

    // Create an aborting trigger that fires ONLY when a note is inserted.
    // This allows the document migration phase to succeed, inserting a document artifact.
    // Then the note migration phase will fail, rolling back the transaction.
    dbFail.execute(
        "CREATE TRIGGER trigger_fail_note BEFORE INSERT ON artifacts FOR EACH ROW WHEN NEW.kind = 'note' BEGIN SELECT "
        "RAISE(ABORT, 'Simulated failure'); END;");

    QVERIFY(!runner25.run(dbFail, &error));

    // Verify rollback
    int version = 0;
    dbFail.queryInt("PRAGMA user_version;", version);
    QCOMPARE(version, 24);

    int schemaVersion = 0;
    dbFail.queryInt("SELECT MAX(version) FROM schema_version;", schemaVersion);
    QCOMPARE(schemaVersion, 24);

    dbFail.queryInt("SELECT COUNT(*) FROM artifacts", count);
    QCOMPARE(count, artifactsBefore);  // should be rolled back to 0

    dbFail.queryInt("SELECT COUNT(*) FROM sqlite_master WHERE name='legacy_document_mapping'", count);
    QCOMPARE(count, 1);  // Document mapping table was created in v24

    // Rows should be rolled back to 0
    dbFail.queryInt("SELECT COUNT(*) FROM legacy_document_mapping", count);
    QCOMPARE(count, 0);
    dbFail.queryInt("SELECT COUNT(*) FROM artifact_versions", count);
    QCOMPARE(count, 0);

    // v25-created mapping tables should be rolled back
    dbFail.queryInt("SELECT COUNT(*) FROM sqlite_master WHERE name='legacy_note_mapping'", count);
    QCOMPARE(count, 0);
    dbFail.queryInt("SELECT COUNT(*) FROM sqlite_master WHERE name='legacy_template_mapping'", count);
    QCOMPARE(count, 0);
    dbFail.queryInt("SELECT COUNT(*) FROM sqlite_master WHERE name='legacy_draft_mapping'", count);
    QCOMPARE(count, 0);

    sqlite3_close(dbHandleFail);
    sqlite3_close(dbHandle);
}

void TestMigrations::testEncryptedFileBackedHistoricalFixtureUpgradeToV25() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.filePath("historical_v21_fixture.db");
    const QByteArray password = "fixture_sqlcipher_secret_key";

    // 1. Create an encrypted file-backed historical v21 database on disk
    sqlite3* rawHandle = nullptr;
    QCOMPARE(sqlite3_open(dbPath.toUtf8().constData(), &rawHandle), SQLITE_OK);

    db::Database rawDb(rawHandle);
    QVERIFY(rawDb.execute(QString("PRAGMA key = '%1';").arg(QString::fromUtf8(password))));
    db::MigrationRunner runner21;
    db::MigrationRunner fullRunner = db::MigrationFactory::createRunner();
    for (const auto& m : fullRunner.getMigrations()) {
        if (m.toVersion <= 21) {
            runner21.addMigration(m);
        }
    }
    QString error;
    QVERIFY2(runner21.run(rawDb, &error), qPrintable(error));

    // Seed colliding numeric IDs (id = 1 across documents, notes, templates, drafts, folders, messages)
    // plus conflicting document_history (newest history row differs from live document content) and NULL fields
    QVERIFY(rawDb.execute(
        "INSERT INTO folders (id, parent_id, name, type, is_expanded) VALUES (1, 0, 'Folder One', 'documents', 1);"));
    QVERIFY(
        rawDb.execute("INSERT INTO messages (id, parent_id, folder_id, role, content) VALUES (1, 0, 1, 'user', "
                      "'Root message content');"));
    QVERIFY(
        rawDb.execute("INSERT INTO documents (id, folder_id, title, content, timestamp, parent_id, metadata) "
                      "VALUES (1, 1, 'Live Doc 1', 'Authoritative Live Content', '2025-02-01 10:00:00', 0, "
                      "'{\"author\":\"tester\"}');"));
    QVERIFY(
        rawDb.execute("INSERT INTO document_history (id, document_id, action_type, content, timestamp) VALUES "
                      "(1, 1, 'manual_edit_pre', 'Newer History Content That Must Not Overwrite Live', "
                      "'2025-02-02 12:00:00');"));
    QVERIFY(rawDb.execute(
        "INSERT INTO notes (id, folder_id, title, content, timestamp) VALUES (1, 1, 'Note 1', 'Live Note 1', "
        "'2025-02-01 10:05:00');"));
    QVERIFY(
        rawDb.execute("INSERT INTO templates (id, folder_id, title, content, timestamp) VALUES (1, 1, 'Template 1', "
                      "'Live Template 1', '2025-02-01 10:10:00');"));
    QVERIFY(
        rawDb.execute("INSERT INTO drafts (id, folder_id, title, content, timestamp, parent_id, target_type) VALUES "
                      "(1, 1, 'Draft 1', 'Draft of Doc 1', '2025-02-01 10:15:00', 1, 'document');"));
    sqlite3_close(rawHandle);

    // 2. Verify the file on disk is actually encrypted (no plaintext strings leaked)
    QFile rawFile(dbPath);
    QVERIFY(rawFile.open(QIODevice::ReadOnly));
    const QByteArray diskBytes = rawFile.readAll();
    rawFile.close();
    QVERIFY(!diskBytes.isEmpty());
    QVERIFY(!diskBytes.contains("Authoritative Live Content"));
    QVERIFY(!diskBytes.contains("SQLite format 3"));

    // 3. Verify opening with wrong password fails cleanly
    {
        BookDatabase wrongDb(dbPath);
        QVERIFY(!wrongDb.open("wrong_password"));
    }

    // 4. Open via BookDatabase with correct password to trigger migration 21 -> 26
    BookDatabase bookDb(dbPath);
    QVERIFY(bookDb.open(QString::fromUtf8(password)));
    db::Database upgradedDb(reinterpret_cast<sqlite3*>(bookDb.getDatabaseHandleForTesting()));

    int userVersion = 0;
    QVERIFY(upgradedDb.queryInt("PRAGMA user_version;", userVersion));
    QCOMPARE(userVersion, 31);

    int artifactCount = 0;
    QVERIFY(upgradedDb.queryInt("SELECT COUNT(*) FROM artifacts;", artifactCount));
    QCOMPARE(artifactCount, 4);

    // Verify live document content is strictly preserved from documents.content, never overwritten by history
    sqlite3_stmt* stmt = nullptr;
    QCOMPARE(sqlite3_prepare_v2(
                 upgradedDb.handle(),
                 "SELECT v.title, v.content FROM artifacts a JOIN artifact_versions v ON v.id = a.current_version_id "
                 "JOIN legacy_document_mapping m ON m.artifact_id = a.id WHERE m.document_id = 1;",
                 -1, &stmt, nullptr),
             SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))), QString("Live Doc 1"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))),
             QString("Authoritative Live Content"));
    sqlite3_finalize(stmt);

    // Verify document_history row was also migrated as a sealed version without becoming current_version_id
    int histMappedCount = 0;
    QVERIFY(upgradedDb.queryInt("SELECT COUNT(*) FROM legacy_document_history_mapping;", histMappedCount));
    QCOMPARE(histMappedCount, 1);

    bookDb.close();
}

void TestMigrations::testMigration26To27HistoryCatchupAndQuarantine() {
    sqlite3* dbHandle = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &dbHandle), SQLITE_OK);
    db::Database db(dbHandle);
    QVERIFY(db.execute("PRAGMA foreign_keys = ON;"));

    db::MigrationRunner fullRunner = db::MigrationFactory::createRunner();
    db::MigrationRunner runner26;
    for (const auto& m : fullRunner.getMigrations()) {
        if (m.toVersion <= 26) {
            runner26.addMigration(m);
        }
    }

    QString error;
    QVERIFY2(runner26.run(db, &error), qPrintable(error));

    // Seed pre-v25 documents & drafts by running 24->25 first, or simulate post-v25 state at v26:
    // Doc 1: mapped in v25, unsealed in v26, then edited in legacy `documents` after v25!
    QVERIFY(
        db.execute("INSERT INTO documents (id, folder_id, title, content, metadata, timestamp) "
                   "VALUES (1, 10, 'Doc 1 Post-V25 Edited Title', 'Doc 1 Post-V25 Live Body', '{\"edited\":true}', "
                   "'2025-03-01 12:00:00');"));
    QVERIFY(
        db.execute("INSERT INTO artifacts (id, kind, folder_id, created_at) "
                   "VALUES (1, 'document', 2, '2025-03-01 09:00:00');"));
    QVERIFY(
        db.execute("INSERT INTO artifact_versions (id, artifact_id, title, content, metadata, is_sealed, "
                   "created_at) VALUES (1, 1, 'Doc 1 Stale V25 Title', 'Doc 1 Stale V25 Body', '', 0, "
                   "'2025-03-01 09:00:00');"));
    QVERIFY(db.execute("UPDATE artifacts SET current_version_id = 1 WHERE id = 1;"));
    QVERIFY(db.execute("INSERT INTO legacy_document_mapping (document_id, artifact_id) VALUES (1, 1);"));

    // Doc 2: mapped in v25, SEALED in v25 (because a draft forked from it), then edited in legacy `documents` after
    // v25!
    QVERIFY(
        db.execute("INSERT INTO documents (id, folder_id, title, content, metadata, timestamp) "
                   "VALUES (2, 20, 'Doc 2 Edited After Seal', 'Doc 2 New Live Body', '{\"v\":2}', "
                   "'2025-03-02 12:00:00');"));
    QVERIFY(
        db.execute("INSERT INTO artifacts (id, kind, folder_id, created_at) "
                   "VALUES (2, 'document', 20, '2025-03-02 09:00:00');"));
    QVERIFY(
        db.execute("INSERT INTO artifact_versions (id, artifact_id, title, content, metadata, is_sealed, "
                   "created_at) VALUES (2, 2, 'Doc 2 Sealed V25', 'Doc 2 Sealed Body', '', 1, "
                   "'2025-03-02 09:00:00');"));
    QVERIFY(db.execute("UPDATE artifacts SET current_version_id = 2 WHERE id = 2;"));
    QVERIFY(db.execute("INSERT INTO legacy_document_mapping (document_id, artifact_id) VALUES (2, 2);"));

    // Doc 3: created in legacy `documents` AFTER v25 (unmapped before v27)
    QVERIFY(
        db.execute("INSERT INTO documents (id, folder_id, title, content, metadata, timestamp) "
                   "VALUES (3, 30, 'Doc 3 Unmapped', 'Doc 3 Live Content', '{\"fresh\":1}', "
                   "'2025-03-03 10:00:00');"));

    // Document history for Doc 1: two historical snapshots (one older, one with a NEWER timestamp than live!)
    QVERIFY(
        db.execute("INSERT INTO document_history (id, document_id, action_type, content, timestamp) VALUES "
                   "(10, 1, 'initial_save', 'Doc 1 History Snapshot 1', '2025-03-01 08:00:00'), "
                   "(11, 1, 'ai_replace', 'Doc 1 History Snapshot 2 (Newer Timestamp)', '2025-03-05 18:00:00');"));

    // Orphaned document_history row (document_id = 999 does not exist)
    QVERIFY(
        db.execute("INSERT INTO document_history (id, document_id, action_type, content, timestamp) VALUES "
                   "(12, 999, 'orphan_edit', 'Orphaned history text', '2025-03-04 11:00:00');"));

    // Draft with valid parent (Doc 3) and Draft with missing parent (document 888)
    QVERIFY(
        db.execute("INSERT INTO drafts (id, folder_id, title, content, parent_id, target_type, timestamp) VALUES "
                   "(1, 30, 'Valid Draft For Doc 3', 'Draft body', 3, 'document', '2025-03-03 11:00:00'), "
                   "(2, 30, 'Orphan Draft', 'Orphan draft body', 888, 'document', '2025-03-03 11:05:00');"));

    // Comment with valid target (Doc 1) and Comment with missing target (note 777)
    QVERIFY(
        db.execute("INSERT INTO comments (id, entity_type, entity_id, content, created_at) VALUES "
                   "(1, 'document', 1, 'Valid comment', '2025-03-01 10:00:00'), "
                   "(2, 'note', 777, 'Orphaned note comment', '2025-03-01 10:05:00');"));

    // Run migration 26 -> 27
    db::MigrationRunner runner27;
    for (const auto& m : fullRunner.getMigrations()) {
        if (m.toVersion == 27) {
            runner27.addMigration(m);
        }
    }
    QVERIFY2(runner27.run(db, &error), qPrintable(error));

    // 1. Verify Doc 1 catch-up and history preservation (live content NOT overwritten by newer history row!)
    sqlite3_stmt* stmt = nullptr;
    QCOMPARE(sqlite3_prepare_v2(dbHandle,
                                "SELECT a.folder_id, v.id, v.title, v.content, v.metadata, v.parent_id "
                                "FROM artifacts a JOIN artifact_versions v ON v.id = a.current_version_id "
                                "WHERE a.id = 1;",
                                -1, &stmt, nullptr),
             SQLITE_OK);
    QCOMPARE(sqlite3_step(stmt), SQLITE_ROW);
    QCOMPARE(sqlite3_column_int(stmt, 0), 10);
    QCOMPARE(sqlite3_column_int(stmt, 1), 1);
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2))),
             QString("Doc 1 Post-V25 Edited Title"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3))),
             QString("Doc 1 Post-V25 Live Body"));
    QCOMPARE(QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))),
             QString("{\"edited\":true}"));
    int doc1LiveParentVerId = sqlite3_column_int(stmt, 5);
    QVERIFY(doc1LiveParentVerId > 0);
    sqlite3_finalize(stmt);

    // Check Doc 1's 2 migrated history versions are sealed and chained
    int doc1VerCount = 0;
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM artifact_versions WHERE artifact_id = 1;", doc1VerCount));
    QCOMPARE(doc1VerCount, 3);  // 2 sealed history versions + 1 live version

    int hist10VerId = 0;
    int hist11VerId = 0;
    QVERIFY(db.queryInt("SELECT artifact_version_id FROM legacy_document_history_mapping WHERE history_id = 10;",
                        hist10VerId));
    QVERIFY(db.queryInt("SELECT artifact_version_id FROM legacy_document_history_mapping WHERE history_id = 11;",
                        hist11VerId));
    QCOMPARE(doc1LiveParentVerId, hist11VerId);

    int hist11ParentId = 0;
    QVERIFY(db.queryInt(QString("SELECT parent_id FROM artifact_versions WHERE id = %1;").arg(hist11VerId),
                        hist11ParentId));
    QCOMPARE(hist11ParentId, hist10VerId);

    // 2. Verify Doc 2 (which had a sealed v25 version) advanced to a new mutable current version
    int doc2CurVerId = 0;
    QVERIFY(db.queryInt("SELECT current_version_id FROM artifacts WHERE id = 2;", doc2CurVerId));
    QVERIFY(doc2CurVerId != 2);
    int doc2ParentId = 0;
    QVERIFY(
        db.queryInt(QString("SELECT parent_id FROM artifact_versions WHERE id = %1;").arg(doc2CurVerId), doc2ParentId));
    QCOMPARE(doc2ParentId, 2);

    // 3. Verify Doc 3 was mapped and Valid Draft For Doc 3 resolved its forked_from_version_id
    int doc3ArtId = 0;
    QVERIFY(db.queryInt("SELECT artifact_id FROM legacy_document_mapping WHERE document_id = 3;", doc3ArtId));
    QVERIFY(doc3ArtId > 0);
    int draft1ForkedFrom = 0;
    QVERIFY(db.queryInt(
        "SELECT v.forked_from_version_id FROM legacy_draft_mapping m JOIN artifacts a ON a.id = m.artifact_id "
        "JOIN artifact_versions v ON v.id = a.current_version_id WHERE m.draft_id = 1;",
        draft1ForkedFrom));
    QVERIFY(draft1ForkedFrom > 0);

    // 4. Verify Quarantined records: 1 orphan document_history + 1 orphan draft + 1 orphan comment = 3
    int quarantinedCount = 0;
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM quarantined_records;", quarantinedCount));
    QCOMPARE(quarantinedCount, 3);

    int qHist = 0, qDraft = 0, qComment = 0;
    QVERIFY(db.queryInt(
        "SELECT COUNT(*) FROM quarantined_records WHERE source_table = 'document_history' AND legacy_id = 12;", qHist));
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM quarantined_records WHERE source_table = 'drafts' AND legacy_id = 2;",
                        qDraft));
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM quarantined_records WHERE source_table = 'comments' AND legacy_id = 2;",
                        qComment));
    QCOMPARE(qHist, 1);
    QCOMPARE(qDraft, 1);
    QCOMPARE(qComment, 1);

    // 5. Verify idempotency when re-running migration 27 logic
    QVERIFY(db.execute("PRAGMA user_version = 26;"));
    QVERIFY(db.execute("DELETE FROM schema_version WHERE version > 26;"));
    QVERIFY2(runner27.run(db, &error), qPrintable(error));

    int quarantinedAfterRerun = 0;
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM quarantined_records;", quarantinedAfterRerun));
    QCOMPARE(quarantinedAfterRerun, 3);

    int fkIssues = 0;
    QVERIFY(db.queryInt("SELECT COUNT(*) FROM pragma_foreign_key_check();", fkIssues));
    QCOMPARE(fkIssues, 0);

    sqlite3_close(dbHandle);
}

QTEST_MAIN(TestMigrations)
#include "test_Migrations.moc"
