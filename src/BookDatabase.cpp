#include "BookDatabase.h"
#include <sqlcipher/sqlite3.h>
#include <QDebug>
#include <QFile>
#include <QVariant>
#include <QDateTime>

BookDatabase::BookDatabase(const QString& filepath) : m_filepath(filepath), m_db(nullptr), m_isOpen(false) {
}

BookDatabase::~BookDatabase() {
    close();
}

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

bool BookDatabase::isOpen() const {
    return m_isOpen;
}

bool BookDatabase::initSchema() {
    if (!m_isOpen) return false;

    const char* sql = "CREATE TABLE IF NOT EXISTS messages ("
                      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                      "parent_id INTEGER, "
                      "role TEXT, "
                      "content TEXT, "
                      "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
                      ");";

    char* errMsg = nullptr;
    int rc = sqlite3_exec((sqlite3*)m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        qWarning() << "SQL error during schema init: " << errMsg;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
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
