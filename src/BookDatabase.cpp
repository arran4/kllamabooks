#include "BookDatabase.h"

#include <sqlcipher/sqlite3.h>

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QVariant>

namespace {
constexpr int CURRENT_SCHEMA_VERSION = 8;
}

BookDatabase::BookDatabase(const QString& filepath) : m_filepath(filepath), m_db(nullptr), m_isOpen(false) {}

BookDatabase::~BookDatabase() { close(); }

bool BookDatabase::open(const QString& password) {
    if (m_isOpen) return true;

    int rc = sqlite3_open(m_filepath.toUtf8().constData(), (sqlite3**)&m_db);
    if (rc != SQLITE_OK) {
        qWarning() << "Cannot open database: " << sqlite3_errmsg((sqlite3*)m_db);
        return false;
    }

    if (!password.isEmpty()) {
        QString keyPragma = QString("PRAGMA key = '%1';").arg(password);
        char* errMsg = nullptr;
        rc = sqlite3_exec((sqlite3*)m_db, keyPragma.toUtf8().constData(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            qWarning() << "Failed to set key: " << errMsg;
            sqlite3_free(errMsg);
            close();
            return false;
        }
    }

    // Verify key works
    char* errMsg = nullptr;
    rc = sqlite3_exec((sqlite3*)m_db, "SELECT count(*) FROM sqlite_master;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        qWarning() << "Invalid password or corrupted database: " << errMsg;
        sqlite3_free(errMsg);
        close();
        return false;
    }

    m_isOpen = true;
    initSchema();

    // Reset processing queue items to pending on reconnect
    sqlite3_exec((sqlite3*)m_db, "UPDATE queue SET status = 'pending' WHERE status = 'processing';", nullptr, nullptr,
                 nullptr);

    return true;
}

void BookDatabase::close() {
    if (m_db) {
        sqlite3_close((sqlite3*)m_db);
        m_db = nullptr;
        m_isOpen = false;
    }
}

bool BookDatabase::isOpen() const { return m_isOpen; }

QString BookDatabase::filepath() const { return m_filepath; }

bool BookDatabase::initSchema() {
    if (!m_isOpen) return false;

    sqlite3_stmt* stmt = nullptr;
    int userVersion = 0;

    // Get existing PRAGMA version for legacy support
    if (sqlite3_prepare_v2((sqlite3*)m_db, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            userVersion = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    bool hasSchemaTable = false;
    if (sqlite3_prepare_v2((sqlite3*)m_db,
                           "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version';", -1, &stmt,
                           nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            hasSchemaTable = true;
        }
        sqlite3_finalize(stmt);
    }

    if (!hasSchemaTable) {
        const char* createSchemaTable =
            "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY, applied_at DATETIME DEFAULT "
            "CURRENT_TIMESTAMP);";
        sqlite3_exec((sqlite3*)m_db, createSchemaTable, nullptr, nullptr, nullptr);
        // Sync PRAGMA version to table if table was just created
        if (userVersion > 0) {
            QString syncSql = QString("INSERT OR REPLACE INTO schema_version (version) VALUES (%1);").arg(userVersion);
            sqlite3_exec((sqlite3*)m_db, syncSql.toUtf8().constData(), nullptr, nullptr, nullptr);
        }
    } else {
        if (sqlite3_prepare_v2((sqlite3*)m_db, "SELECT MAX(version) FROM schema_version;", -1, &stmt, nullptr) ==
            SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                int tableVersion = sqlite3_column_int(stmt, 0);
                if (tableVersion > userVersion) userVersion = tableVersion;
            }
            sqlite3_finalize(stmt);
        }
    }

    if (userVersion == 0) {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "parent_id INTEGER, "
            "folder_id INTEGER DEFAULT 0, "
            "role TEXT, "
            "content TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS documents ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "folder_id INTEGER DEFAULT 0, "
            "title TEXT, "
            "content TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS templates ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "folder_id INTEGER DEFAULT 0, "
            "title TEXT, "
            "content TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS drafts ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "folder_id INTEGER DEFAULT 0, "
            "title TEXT, "
            "content TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS notes ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "folder_id INTEGER DEFAULT 0, "
            "title TEXT, "
            "content TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS folders ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "parent_id INTEGER DEFAULT 0, "
            "name TEXT, "
            "type TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
            "position INTEGER DEFAULT 0"
            ");"
            "CREATE TABLE IF NOT EXISTS settings ("
            "scope TEXT, "
            "target_id INTEGER, "
            "key TEXT, "
            "value TEXT, "
            "PRIMARY KEY(scope, target_id, key)"
            ");"
            "INSERT INTO schema_version (version) VALUES (1);";

        char* errMsg = nullptr;
        int rc = sqlite3_exec((sqlite3*)m_db, sql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            qWarning() << "SQL error during schema init: " << errMsg;
            sqlite3_free(errMsg);
            return false;
        }

        sqlite3_exec((sqlite3*)m_db, "PRAGMA user_version = 1;", nullptr, nullptr, nullptr);
        userVersion = 1;
    }

    if (userVersion < 2) {
        // upgrade to version 2 - Ensure settings table exists (redundant but safe)
        sqlite3_exec((sqlite3*)m_db,
                     "CREATE TABLE IF NOT EXISTS settings (scope TEXT, target_id INTEGER, key TEXT, value TEXT, "
                     "PRIMARY KEY(scope, target_id, key));",
                     nullptr, nullptr, nullptr);
        sqlite3_exec((sqlite3*)m_db, "INSERT OR REPLACE INTO schema_version (version) VALUES (2);", nullptr, nullptr,
                     nullptr);
        sqlite3_exec((sqlite3*)m_db, "PRAGMA user_version = 2;", nullptr, nullptr, nullptr);
        userVersion = 2;
    }

    if (userVersion < 3) {
        // upgrade to version 3: folders, templates, drafts, folder_id columns
        const char* sql =
            "CREATE TABLE IF NOT EXISTS templates (id INTEGER PRIMARY KEY AUTOINCREMENT, folder_id INTEGER DEFAULT 0, "
            "title TEXT, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);"
            "CREATE TABLE IF NOT EXISTS drafts (id INTEGER PRIMARY KEY AUTOINCREMENT, folder_id INTEGER DEFAULT 0, "
            "title TEXT, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);"
            "CREATE TABLE IF NOT EXISTS folders (id INTEGER PRIMARY KEY AUTOINCREMENT, parent_id INTEGER DEFAULT 0, "
            "name TEXT, type TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, position INTEGER DEFAULT 0);";

        sqlite3_exec((sqlite3*)m_db, sql, nullptr, nullptr, nullptr);

        // Add folder_id columns if they don't exist
        sqlite3_exec((sqlite3*)m_db, "ALTER TABLE documents ADD COLUMN folder_id INTEGER DEFAULT 0;", nullptr, nullptr,
                     nullptr);
        sqlite3_exec((sqlite3*)m_db, "ALTER TABLE notes ADD COLUMN folder_id INTEGER DEFAULT 0;", nullptr, nullptr,
                     nullptr);

        sqlite3_exec((sqlite3*)m_db, "INSERT OR REPLACE INTO schema_version (version) VALUES (3);", nullptr, nullptr,
                     nullptr);
        sqlite3_exec((sqlite3*)m_db, "PRAGMA user_version = 3;", nullptr, nullptr, nullptr);
        userVersion = 3;
    }

    if (userVersion < 4) {
        sqlite3_exec((sqlite3*)m_db, "ALTER TABLE messages ADD COLUMN folder_id INTEGER DEFAULT 0;", nullptr, nullptr,
                     nullptr);
        sqlite3_exec((sqlite3*)m_db, "INSERT OR REPLACE INTO schema_version (version) VALUES (4);", nullptr, nullptr,
                     nullptr);
        sqlite3_exec((sqlite3*)m_db, "PRAGMA user_version = 4;", nullptr, nullptr, nullptr);
        userVersion = 4;
    }

    if (userVersion < 5) {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS queue ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "message_id INTEGER, "
            "model TEXT, "
            "prompt TEXT, "
            "status TEXT, "  // pending, processing, completed, error, paused
            "priority INTEGER DEFAULT 0, "
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS notifications ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "message_id INTEGER, "
            "type TEXT, "  // responded_to, error
            "is_dismissed BOOLEAN DEFAULT 0, "
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";
        sqlite3_exec((sqlite3*)m_db, sql, nullptr, nullptr, nullptr);
        sqlite3_exec((sqlite3*)m_db, "INSERT OR REPLACE INTO schema_version (version) VALUES (5);", nullptr, nullptr,
                     nullptr);
        sqlite3_exec((sqlite3*)m_db, "PRAGMA user_version = 5;", nullptr, nullptr, nullptr);
        userVersion = 5;
    }

    if (userVersion < 6) {
        // Clean up old completed jobs since they are now memory-only
        sqlite3_exec((sqlite3*)m_db, "DELETE FROM queue WHERE status = 'completed';", nullptr, nullptr, nullptr);
        sqlite3_exec((sqlite3*)m_db, "INSERT OR REPLACE INTO schema_version (version) VALUES (6);", nullptr, nullptr,
                     nullptr);
        sqlite3_exec((sqlite3*)m_db, "PRAGMA user_version = 6;", nullptr, nullptr, nullptr);
        userVersion = 6;
    }

    if (userVersion < 7) {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS comments ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "entity_type TEXT, "
            "entity_id INTEGER, "
            "content TEXT, "
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";
        sqlite3_exec((sqlite3*)m_db, sql, nullptr, nullptr, nullptr);
        sqlite3_exec((sqlite3*)m_db, "INSERT OR REPLACE INTO schema_version (version) VALUES (7);", nullptr, nullptr,
                     nullptr);
        sqlite3_exec((sqlite3*)m_db, "PRAGMA user_version = 7;", nullptr, nullptr, nullptr);
        userVersion = 7;
    }

    if (userVersion < 8) {
        sqlite3_exec((sqlite3*)m_db, "ALTER TABLE documents ADD COLUMN parent_id INTEGER DEFAULT 0;", nullptr, nullptr,
                     nullptr);
        sqlite3_exec((sqlite3*)m_db, "INSERT OR REPLACE INTO schema_version (version) VALUES (8);", nullptr, nullptr,
                     nullptr);
        sqlite3_exec((sqlite3*)m_db, "PRAGMA user_version = 8;", nullptr, nullptr, nullptr);
        userVersion = 8;
    }

    return true;
}

void BookDatabase::setSetting(const QString& scope, int targetId, const QString& key, const QString& value) {
    if (!m_isOpen) return;

    const char* sql = "INSERT OR REPLACE INTO settings (scope, target_id, key, value) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, scope.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, targetId);
    sqlite3_bind_text(stmt, 3, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, value.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

QString BookDatabase::getSetting(const QString& scope, int targetId, const QString& key,
                                 const QString& defaultValue) const {
    if (!m_isOpen) return defaultValue;

    const char* sql = "SELECT value FROM settings WHERE scope = ? AND target_id = ? AND key = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return defaultValue;

    sqlite3_bind_text(stmt, 1, scope.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, targetId);
    sqlite3_bind_text(stmt, 3, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    QString result = defaultValue;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool BookDatabase::moveItem(const QString& table, int id, int newFolderId) {
    if (!m_isOpen) return false;
    // Map of safe table names to prevent injection
    QString safeTable = table;
    if (table != "messages" && table != "documents" && table != "templates" && table != "drafts" && table != "notes") {
        return false;
    }

    QString sql = QString("UPDATE %1 SET folder_id = ? WHERE id = ?;").arg(safeTable);
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, newFolderId);
    sqlite3_bind_int(stmt, 2, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool BookDatabase::moveFolder(int id, int newParentId) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE folders SET parent_id = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, newParentId);
    sqlite3_bind_int(stmt, 2, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

int BookDatabase::addMessage(int parentId, const QString& content, const QString& role, int folderId) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO messages (parent_id, folder_id, role, content) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, parentId);
    sqlite3_bind_int(stmt, 2, folderId);
    sqlite3_bind_text(stmt, 3, role.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid((sqlite3*)m_db);
    sqlite3_finalize(stmt);
    return id;
}

bool BookDatabase::updateMessage(int id, const QString& newContent) {
    if (!m_isOpen) return false;

    const char* sql = "UPDATE messages SET content = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, newContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::deleteMessage(int id) {
    if (!m_isOpen) return false;

    // Recursively delete children? For now just delete this one.
    const char* sql = "DELETE FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int BookDatabase::getRootMessageId(int messageId) const {
    if (!m_isOpen || messageId <= 0) return messageId;

    int currentId = messageId;
    const char* sql = "SELECT parent_id FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return currentId;
    }

    while (currentId > 0) {
        int parentId = 0;
        sqlite3_bind_int(stmt, 1, currentId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            parentId = sqlite3_column_int(stmt, 0);
        }
        sqlite3_reset(stmt);

        if (parentId <= 0) {
            break;
        }
        currentId = parentId;
    }
    sqlite3_finalize(stmt);
    return currentId;
}

QString BookDatabase::getInheritedSetting(int messageId, const QString& key) const {
    if (!m_isOpen) return QString();

    int currentId = messageId;
    const char* sql = "SELECT parent_id FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return QString();
    }

    QString inheritedValue;
    while (currentId > 0) {
        QString currentValue = getSetting("chat", currentId, key, "");
        if (!currentValue.isEmpty() && currentValue != "default") {
            inheritedValue = currentValue;
            break;
        }

        int parentId = 0;
        sqlite3_bind_int(stmt, 1, currentId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            parentId = sqlite3_column_int(stmt, 0);
        }
        sqlite3_reset(stmt);

        if (parentId <= 0) {
            break;
        }
        currentId = parentId;
    }
    sqlite3_finalize(stmt);
    return inheritedValue;
}

QList<MessageNode> BookDatabase::getMessages() const {
    QList<MessageNode> nodes;
    if (!m_isOpen) return nodes;

    const char* sql = "SELECT id, parent_id, folder_id, role, content, timestamp FROM messages ORDER BY id;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MessageNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.parentId = sqlite3_column_int(stmt, 1);
        node.folderId = sqlite3_column_int(stmt, 2);
        node.role = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        node.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 5));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
}

int BookDatabase::addDocument(int folderId, const QString& title, const QString& content, int parentId) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO documents (folder_id, title, content, parent_id) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, folderId);
    sqlite3_bind_text(stmt, 2, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, parentId);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid((sqlite3*)m_db);
    sqlite3_finalize(stmt);
    return id;
}

bool BookDatabase::updateDocument(int id, const QString& newTitle, const QString& newContent) {
    if (!m_isOpen) return false;

    const char* sql = "UPDATE documents SET title = ?, content = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, newTitle.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, newContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

QList<DocumentNode> BookDatabase::getDocuments(int folderId) const {
    QList<DocumentNode> nodes;
    if (!m_isOpen) return nodes;

    QString sqlStr = "SELECT id, folder_id, title, content, timestamp, parent_id FROM documents";
    if (folderId != -1) sqlStr += " WHERE folder_id = ?";
    sqlStr += " ORDER BY timestamp ASC;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sqlStr.toUtf8().constData(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    if (folderId != -1) sqlite3_bind_int(stmt, 1, folderId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DocumentNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.folderId = sqlite3_column_int(stmt, 1);
        node.title = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        node.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        node.parentId = sqlite3_column_int(stmt, 5);
        node.isFolder = false;
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
}

bool BookDatabase::deleteDocument(int id) {
    if (!m_isOpen) return false;
    const char* sql = "DELETE FROM documents WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int BookDatabase::addNote(int folderId, const QString& title, const QString& content) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO notes (folder_id, title, content) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, folderId);
    sqlite3_bind_text(stmt, 2, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid((sqlite3*)m_db);
    sqlite3_finalize(stmt);
    return id;
}

bool BookDatabase::updateNote(int id, const QString& newTitle, const QString& newContent) {
    if (!m_isOpen) return false;

    const char* sql = "UPDATE notes SET title = ?, content = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, newTitle.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, newContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

QList<NoteNode> BookDatabase::getNotes(int folderId) const {
    QList<NoteNode> nodes;
    if (!m_isOpen) return nodes;

    QString sqlStr = "SELECT id, folder_id, title, content, timestamp FROM notes";
    if (folderId != -1) sqlStr += " WHERE folder_id = ?";
    sqlStr += " ORDER BY id;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sqlStr.toUtf8().constData(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    if (folderId != -1) sqlite3_bind_int(stmt, 1, folderId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        NoteNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.folderId = sqlite3_column_int(stmt, 1);
        node.title = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        node.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
}

bool BookDatabase::deleteNote(int id) {
    if (!m_isOpen) return false;
    const char* sql = "DELETE FROM notes WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int BookDatabase::addTemplate(int folderId, const QString& title, const QString& content) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO templates (folder_id, title, content) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, folderId);
    sqlite3_bind_text(stmt, 2, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid((sqlite3*)m_db);
    sqlite3_finalize(stmt);
    return id;
}

bool BookDatabase::updateTemplate(int id, const QString& newTitle, const QString& newContent) {
    if (!m_isOpen) return false;

    const char* sql = "UPDATE templates SET title = ?, content = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, newTitle.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, newContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

QList<DocumentNode> BookDatabase::getTemplates(int folderId) const {
    QList<DocumentNode> nodes;
    if (!m_isOpen) return nodes;

    QString sqlStr = "SELECT id, folder_id, title, content, timestamp FROM templates";
    if (folderId != -1) sqlStr += " WHERE folder_id = ?";
    sqlStr += " ORDER BY timestamp ASC;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sqlStr.toUtf8().constData(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    if (folderId != -1) sqlite3_bind_int(stmt, 1, folderId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DocumentNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.folderId = sqlite3_column_int(stmt, 1);
        node.title = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        node.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        node.isFolder = false;
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
}

int BookDatabase::addDraft(int folderId, const QString& title, const QString& content) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO drafts (folder_id, title, content) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, folderId);
    sqlite3_bind_text(stmt, 2, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid((sqlite3*)m_db);
    sqlite3_finalize(stmt);
    return id;
}

bool BookDatabase::updateDraft(int id, const QString& newTitle, const QString& newContent) {
    if (!m_isOpen) return false;

    const char* sql = "UPDATE drafts SET title = ?, content = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, newTitle.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, newContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

QList<DocumentNode> BookDatabase::getDrafts(int folderId) const {
    QList<DocumentNode> nodes;
    if (!m_isOpen) return nodes;

    QString sqlStr = "SELECT id, folder_id, title, content, timestamp FROM drafts";
    if (folderId != -1) sqlStr += " WHERE folder_id = ?";
    sqlStr += " ORDER BY timestamp ASC;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sqlStr.toUtf8().constData(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    if (folderId != -1) sqlite3_bind_int(stmt, 1, folderId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DocumentNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.folderId = sqlite3_column_int(stmt, 1);
        node.title = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        node.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        node.isFolder = false;
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
}

bool BookDatabase::deleteDraft(int id) {
    if (!m_isOpen) return false;
    const char* sql = "DELETE FROM drafts WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int BookDatabase::addFolder(int parentId, const QString& name, const QString& type) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO folders (parent_id, name, type) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, parentId);
    sqlite3_bind_text(stmt, 2, name.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, type.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid((sqlite3*)m_db);
    sqlite3_finalize(stmt);
    return id;
}

bool BookDatabase::updateFolder(int id, const QString& newName) {
    if (!m_isOpen) return false;

    const char* sql = "UPDATE folders SET name = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, newName.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::deleteFolder(int id) {
    if (!m_isOpen) return false;

    const char* sql = "DELETE FROM folders WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

QList<FolderNode> BookDatabase::getFolders(const QString& type) const {
    QList<FolderNode> folderNodes;
    if (!m_isOpen) return folderNodes;

    const char* sql =
        "SELECT id, parent_id, name, type, timestamp, position FROM folders WHERE type = ? ORDER BY position ASC, name "
        "ASC;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return folderNodes;

    sqlite3_bind_text(stmt, 1, type.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FolderNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.parentId = sqlite3_column_int(stmt, 1);
        node.name = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        node.type = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        node.position = sqlite3_column_int(stmt, 5);
        folderNodes.append(node);
    }
    sqlite3_finalize(stmt);
    return folderNodes;
}

int BookDatabase::enqueuePrompt(int messageId, const QString& model, const QString& prompt, int priority) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO queue (message_id, model, prompt, status, priority) VALUES (?, ?, ?, 'pending', ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, messageId);
    sqlite3_bind_text(stmt, 2, model.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, prompt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, priority);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid((sqlite3*)m_db);
    sqlite3_finalize(stmt);
    return id;
}

QList<QueueItem> BookDatabase::getQueue() const {
    QList<QueueItem> items;
    if (!m_isOpen) return items;

    const char* sql =
        "SELECT id, message_id, model, prompt, status, priority, created_at FROM queue ORDER BY priority DESC, "
        "created_at DESC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return items;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QueueItem item;
        item.id = sqlite3_column_int(stmt, 0);
        item.messageId = sqlite3_column_int(stmt, 1);
        item.model = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        item.prompt = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        item.status = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        item.priority = sqlite3_column_int(stmt, 5);
        item.timestamp =
            QDateTime::fromString(QString::fromUtf8((const char*)sqlite3_column_text(stmt, 6)), Qt::ISODate);
        items.append(item);
    }
    sqlite3_finalize(stmt);
    return items;
}

bool BookDatabase::updateQueueStatus(int id, const QString& status) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE queue SET status = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, status.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::updateQueueItemPrompt(int id, const QString& prompt) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE queue SET prompt = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, prompt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::deleteQueueItem(int id) {
    if (!m_isOpen) return false;
    const char* sql = "DELETE FROM queue WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int BookDatabase::addNotification(int messageId, const QString& type) {
    if (!m_isOpen) return -1;
    const char* sql = "INSERT INTO notifications (message_id, type) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, messageId);
    sqlite3_bind_text(stmt, 2, type.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int id = sqlite3_last_insert_rowid((sqlite3*)m_db);
    sqlite3_finalize(stmt);
    return id;
}

QList<Notification> BookDatabase::getNotifications(bool includeDismissed) const {
    QList<Notification> notifications;
    if (!m_isOpen) return notifications;
    QString sql = "SELECT id, message_id, type, is_dismissed, created_at FROM notifications";
    if (!includeDismissed) sql += " WHERE is_dismissed = 0";
    sql += " ORDER BY created_at DESC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK)
        return notifications;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Notification n;
        n.id = sqlite3_column_int(stmt, 0);
        n.messageId = sqlite3_column_int(stmt, 1);
        n.type = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        n.isDismissed = sqlite3_column_int(stmt, 3) != 0;
        n.timestamp = QDateTime::fromString(QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4)), Qt::ISODate);
        notifications.append(n);
    }
    sqlite3_finalize(stmt);
    return notifications;
}

bool BookDatabase::dismissNotification(int id) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE notifications SET is_dismissed = 1 WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::dismissNotificationByMessageId(int messageId) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE notifications SET is_dismissed = 1 WHERE message_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, messageId);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

QString BookDatabase::getDatabaseDebugInfo() const {
    if (!m_isOpen) return "Database not open.";

    QString info;
    int userVersion = 0;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            userVersion = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    info += QString("Schema Version: %1\n\n").arg(userVersion);

    info += "Tables:\n";
    if (sqlite3_prepare_v2((sqlite3*)m_db, "SELECT name, sql FROM sqlite_master WHERE type='table';", -1, &stmt,
                           nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* namePtr = sqlite3_column_text(stmt, 0);
            QString name = namePtr ? QString::fromUtf8((const char*)namePtr) : "";
            info += QString("- %1\n").arg(name);
        }
        sqlite3_finalize(stmt);
    }

    return info;
}

int BookDatabase::addComment(const QString& entityType, int entityId, const QString& content) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO comments (entity_type, entity_id, content) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, entityType.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, entityId);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid((sqlite3*)m_db);
    sqlite3_finalize(stmt);
    return id;
}

bool BookDatabase::updateComment(int id, const QString& newContent) {
    if (!m_isOpen) return false;

    const char* sql = "UPDATE comments SET content = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, newContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

QList<CommentNode> BookDatabase::getComments(const QString& entityType, int entityId) const {
    QList<CommentNode> items;
    if (!m_isOpen) return items;

    const char* sql =
        "SELECT id, entity_type, entity_id, content, created_at FROM comments WHERE entity_type = ? AND entity_id = ? "
        "ORDER BY created_at ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return items;

    sqlite3_bind_text(stmt, 1, entityType.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, entityId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CommentNode item;
        item.id = sqlite3_column_int(stmt, 0);
        item.entityType = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        item.entityId = sqlite3_column_int(stmt, 2);
        item.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        item.timestamp =
            QDateTime::fromString(QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4)), Qt::ISODate);
        items.append(item);
    }

    sqlite3_finalize(stmt);
    return items;
}

bool BookDatabase::deleteComment(int id) {
    if (!m_isOpen) return false;
    const char* sql = "DELETE FROM comments WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}
