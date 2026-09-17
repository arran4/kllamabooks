import re

content = open('src/db/Database.h').read()
if 'bool hasColumn' not in content:
    content = content.replace(
        'bool queryInt(const QString& sql, int& result, QString* error = nullptr);',
        'bool queryInt(const QString& sql, int& result, QString* error = nullptr);\n    bool hasColumn(const QString& table, const QString& column, QString* error = nullptr);'
    )
    with open('src/db/Database.h', 'w') as f:
        f.write(content)

content = open('src/db/Database.cpp').read()
if 'Database::hasColumn' not in content:
    content += """
bool Database::hasColumn(const QString& table, const QString& column, QString* error) {
    QString sql = "PRAGMA table_info(" + table + ");";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return false;
    }

    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QString name = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        if (name == column) {
            found = true;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return found;
}
"""
    with open('src/db/Database.cpp', 'w') as f:
        f.write(content)

content = open('src/db/MigrationFactory.cpp').read()

# 2->3
content = content.replace(
    'db.execute("ALTER TABLE documents ADD COLUMN folder_id INTEGER DEFAULT 0;");\n             db.execute("ALTER TABLE notes ADD COLUMN folder_id INTEGER DEFAULT 0;");\n             return ok;',
    'if (!db.hasColumn("documents", "folder_id")) { ok = ok && db.execute("ALTER TABLE documents ADD COLUMN folder_id INTEGER DEFAULT 0;"); }\n             if (!db.hasColumn("notes", "folder_id")) { ok = ok && db.execute("ALTER TABLE notes ADD COLUMN folder_id INTEGER DEFAULT 0;"); }\n             return ok;'
)

# 3->4
content = content.replace(
    'db.execute("ALTER TABLE messages ADD COLUMN folder_id INTEGER DEFAULT 0;");\n                             return true;',
    'if (!db.hasColumn("messages", "folder_id")) { return db.execute("ALTER TABLE messages ADD COLUMN folder_id INTEGER DEFAULT 0;"); }\n                             return true;'
)

# 7->8
content = content.replace(
    'db.execute("ALTER TABLE documents ADD COLUMN parent_id INTEGER DEFAULT 0;");\n                             return true;',
    'if (!db.hasColumn("documents", "parent_id")) { return db.execute("ALTER TABLE documents ADD COLUMN parent_id INTEGER DEFAULT 0;"); }\n                             return true;'
)

with open('src/db/MigrationFactory.cpp', 'w') as f:
    f.write(content)
