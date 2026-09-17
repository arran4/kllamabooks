import re

content = open('tests/test_Migrations.cpp').read()
content = content.replace(
    'void testFailingAlterRollback();',
    'void testFailingAlterRollback();\n    void testVersionDisagreement();'
)

content += """
void TestMigrations::testVersionDisagreement() {
    sqlite3* dbHandle;
    sqlite3_open(":memory:", &dbHandle);
    db::Database db(dbHandle);

    db.execute("CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
    db.execute("INSERT INTO schema_version (version) VALUES (2);");
    db.execute("PRAGMA user_version = 3;"); // Disagreement!

    db::MigrationRunner runner;
    runner.addMigration({3, 4, "test", [](db::Database& d) {
        return d.execute("CREATE TABLE t1 (id INTEGER);");
    }});

    QString error;
    // Actually, based on current implementation, it picks the max.
    // The instructions say: "Do not silently choose the higher version when the two sources disagree."
    // Let's modify Migrations.cpp to throw an error!
    QVERIFY(!runner.run(db, &error));
    QVERIFY(error.contains("Version disagreement"));

    sqlite3_close(dbHandle);
}
"""
with open('tests/test_Migrations.cpp', 'w') as f:
    f.write(content)
