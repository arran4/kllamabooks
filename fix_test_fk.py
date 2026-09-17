import re

content = open('tests/test_Migrations.cpp').read()
content = content.replace(
    '''void TestMigrations::testForeignKeysEnabled() {
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
}''',
    '''#include "../src/BookDatabase.h"
void TestMigrations::testForeignKeysEnabled() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    // Test that the production connection correctly enabled foreign keys
    db::Database dbAccess(reinterpret_cast<sqlite3*>(db.getDatabaseHandleForTesting())); // We'll add this accessor

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
}'''
)
with open('tests/test_Migrations.cpp', 'w') as f:
    f.write(content)

content = open('src/BookDatabase.h').read()
content = content.replace(
    'QString getDatabaseDebugInfo() const;',
    'QString getDatabaseDebugInfo() const;\n    void* getDatabaseHandleForTesting() const { return m_db; }'
)
with open('src/BookDatabase.h', 'w') as f:
    f.write(content)

content = open('src/BookDatabase.cpp').read()
content = content.replace(
    '// Enable foreign keys\n    sqlite3_exec(reinterpret_cast<sqlite3*>(m_db), "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);',
    '// Enable foreign keys\n    if (sqlite3_exec(reinterpret_cast<sqlite3*>(m_db), "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr) != SQLITE_OK) {\n        qWarning() << "Failed to enable foreign keys";\n        close();\n        return false;\n    }'
)
with open('src/BookDatabase.cpp', 'w') as f:
    f.write(content)
