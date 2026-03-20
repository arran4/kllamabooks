#include "BookDatabase.h"

#include <sqlcipher/sqlite3.h>

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QVariant>

namespace {
constexpr int CURRENT_SCHEMA_VERSION = 3;
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

    int userVersion = 0;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)m_db, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            userVersion = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (userVersion == 0) {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "parent_id INTEGER, "
            "role TEXT, "
            "content TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS documents ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "parent_id INTEGER, "
            "title TEXT, "
            "content TEXT, "
            "is_folder INTEGER DEFAULT 0, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS notes ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "title TEXT, "
            "content TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS items ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "type TEXT, "
            "name TEXT, "
            "root_id INTEGER"
            ");"
            "CREATE TABLE IF NOT EXISTS settings ("
            "scope TEXT, "
            "target_id INTEGER, "
            "key TEXT, "
            "value TEXT, "
            "PRIMARY KEY(scope, target_id, key)"
            ");"
            "CREATE TABLE IF NOT EXISTS templates ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "parent_id INTEGER, "
            "title TEXT, "
            "content TEXT, "
            "is_folder INTEGER DEFAULT 0, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS drafts ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "parent_id INTEGER, "
            "title TEXT, "
            "content TEXT, "
            "is_folder INTEGER DEFAULT 0, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";

        char* errMsg = nullptr;
        int rc = sqlite3_exec((sqlite3*)m_db, sql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            qWarning() << "SQL error during schema init: " << errMsg;
            sqlite3_free(errMsg);
            return false;
        }

        QString versionSql = QString("PRAGMA user_version = 1;");
        sqlite3_exec((sqlite3*)m_db, versionSql.toUtf8().constData(), nullptr, nullptr, nullptr);
        userVersion = 1;
    }

    if (userVersion < 2) {
        // upgrade to version 2
        const char* sql =
            "CREATE TABLE IF NOT EXISTS settings ("
            "scope TEXT, "
            "target_id INTEGER, "
            "key TEXT, "
            "value TEXT, "
            "PRIMARY KEY(scope, target_id, key)"
            ");"
            "CREATE TABLE IF NOT EXISTS documents ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "parent_id INTEGER, "
            "title TEXT, "
            "content TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS notes ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "title TEXT, "
            "content TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";

        char* errMsg = nullptr;
        int rc = sqlite3_exec((sqlite3*)m_db, sql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            qWarning() << "SQL error during schema migration to v2: " << errMsg;
            sqlite3_free(errMsg);
            return false;
        }

        QString versionSql = QString("PRAGMA user_version = 2;");
        sqlite3_exec((sqlite3*)m_db, versionSql.toUtf8().constData(), nullptr, nullptr, nullptr);
        userVersion = 2;
    }

    if (userVersion < 3) {
        // upgrade to version 3
        const char* alterSql = "ALTER TABLE documents ADD COLUMN is_folder INTEGER DEFAULT 0;";
        sqlite3_exec((sqlite3*)m_db, alterSql, nullptr, nullptr, nullptr); // Ignore errors if it already exists

        const char* sql =
            "CREATE TABLE IF NOT EXISTS templates ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "parent_id INTEGER, "
            "title TEXT, "
            "content TEXT, "
            "is_folder INTEGER DEFAULT 0, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");"
            "CREATE TABLE IF NOT EXISTS drafts ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "parent_id INTEGER, "
            "title TEXT, "
            "content TEXT, "
            "is_folder INTEGER DEFAULT 0, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";

        char* errMsg = nullptr;
        int rc = sqlite3_exec((sqlite3*)m_db, sql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            qWarning() << "SQL error during schema migration to v3: " << errMsg;
            sqlite3_free(errMsg);
            return false;
        }

        QString versionSql = QString("PRAGMA user_version = 3;");
        sqlite3_exec((sqlite3*)m_db, versionSql.toUtf8().constData(), nullptr, nullptr, nullptr);
        userVersion = 3;
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

int BookDatabase::addMessage(int parentId, const QString& content, const QString& role) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO messages (parent_id, role, content) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, parentId);
    sqlite3_bind_text(stmt, 2, role.toUtf8().constData(), -1, SQLITE_TRANSIENT);
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

QList<MessageNode> BookDatabase::getMessages() const {
    QList<MessageNode> nodes;
    if (!m_isOpen) return nodes;

    const char* sql = "SELECT id, parent_id, role, content, timestamp FROM messages ORDER BY id;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MessageNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.parentId = sqlite3_column_int(stmt, 1);
        node.role = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        node.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
}

int BookDatabase::addDocument(int parentId, const QString& title, const QString& content, bool isFolder) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO documents (parent_id, title, content, is_folder) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, parentId);
    sqlite3_bind_text(stmt, 2, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, isFolder ? 1 : 0);

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

QList<DocumentNode> BookDatabase::getDocuments() const {
    QList<DocumentNode> nodes;
    if (!m_isOpen) return nodes;

    const char* sql =
        "SELECT id, parent_id, title, content, timestamp, is_folder FROM documents ORDER BY timestamp ASC;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DocumentNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.parentId = sqlite3_column_int(stmt, 1);
        node.title = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        node.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        node.isFolder = sqlite3_column_int(stmt, 5) > 0;
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
}

int BookDatabase::addNote(const QString& title, const QString& content) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO notes (title, content) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);

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

QList<NoteNode> BookDatabase::getNotes() const {
    QList<NoteNode> nodes;
    if (!m_isOpen) return nodes;

    const char* sql = "SELECT id, title, content, timestamp FROM notes ORDER BY id;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        NoteNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.title = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        node.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
}

int BookDatabase::addTemplate(int parentId, const QString& title, const QString& content, bool isFolder) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO templates (parent_id, title, content, is_folder) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, parentId);
    sqlite3_bind_text(stmt, 2, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, isFolder ? 1 : 0);

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

QList<DocumentNode> BookDatabase::getTemplates() const {
    QList<DocumentNode> nodes;
    if (!m_isOpen) return nodes;

    const char* sql =
        "SELECT id, parent_id, title, content, timestamp, is_folder FROM templates ORDER BY timestamp ASC;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DocumentNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.parentId = sqlite3_column_int(stmt, 1);
        node.title = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        node.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        node.isFolder = sqlite3_column_int(stmt, 5) > 0;
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
}

int BookDatabase::addDraft(int parentId, const QString& title, const QString& content, bool isFolder) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO drafts (parent_id, title, content, is_folder) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, parentId);
    sqlite3_bind_text(stmt, 2, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, isFolder ? 1 : 0);

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

QList<DocumentNode> BookDatabase::getDrafts() const {
    QList<DocumentNode> nodes;
    if (!m_isOpen) return nodes;

    const char* sql = "SELECT id, parent_id, title, content, timestamp, is_folder FROM drafts ORDER BY timestamp ASC;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DocumentNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.parentId = sqlite3_column_int(stmt, 1);
        node.title = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        node.content = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        QString ts = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        node.isFolder = sqlite3_column_int(stmt, 5) > 0;
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
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
    if (sqlite3_prepare_v2((sqlite3*)m_db, "SELECT name, sql FROM sqlite_master WHERE type='table';", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* namePtr = sqlite3_column_text(stmt, 0);
            QString name = namePtr ? QString::fromUtf8((const char*)namePtr) : "";
            info += QString("- %1\n").arg(name);
        }
        sqlite3_finalize(stmt);
    }

    return info;
}
