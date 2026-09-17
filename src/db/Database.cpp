#include "Database.h"

#include <sqlcipher/sqlite3.h>

namespace db {

Database::Database(sqlite3* handle) : m_db(handle) {}

bool Database::execute(const QString& sql, QString* error) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (error) {
            *error = QString::fromUtf8(errMsg);
        }
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool Database::queryInt(const QString& sql, int& result, QString* error) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (error) {
            *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        }
        return false;
    }

    rc = sqlite3_step(stmt);
    bool success = false;
    if (rc == SQLITE_ROW) {
        result = sqlite3_column_int(stmt, 0);
        success = true;
    } else if (rc == SQLITE_DONE) {
        if (error) *error = "No rows returned";
        success = false;
    } else {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        success = false;
    }

    sqlite3_finalize(stmt);
    return success;
}

}  // namespace db

bool db::Database::hasColumn(const QString& table, const QString& column, QString* error) {
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
