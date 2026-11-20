#include "ActivityStore.h"

#include <sqlcipher/sqlite3.h>

#include <algorithm>

namespace store {

namespace {

QString colText(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return QString();
    const unsigned char* txt = sqlite3_column_text(stmt, idx);
    return txt ? QString::fromUtf8(reinterpret_cast<const char*>(txt)) : QString();
}

}  // namespace

// ============================================================================
// Constructor
// ============================================================================

ActivityStore::ActivityStore(db::Database& db) : m_db(db) {}

// ============================================================================
// Public Methods
// ============================================================================

Result<int> ActivityStore::addNotification(int targetId, const QString& targetType, const QString& type,
                                           ActivitySeverity severity, const QString& title, const QString& message,
                                           int runId) {
    if (!m_db.handle()) return Result<int>::fail(TransitionError::DatabaseError, "Database not open");

    const char* sql =
        "INSERT INTO notifications (target_id, target_type, type, run_id, severity, title, message) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<int>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }

    const QByteArray ttUtf8 = targetType.toUtf8();
    const QByteArray tyUtf8 = type.toUtf8();
    const QByteArray svUtf8 = activitySeverityToString(severity).toUtf8();
    const QByteArray tlUtf8 = title.toUtf8();
    const QByteArray msgUtf8 = message.toUtf8();

    sqlite3_bind_int(stmt, 1, targetId);
    sqlite3_bind_text(stmt, 2, ttUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, tyUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, runId);
    sqlite3_bind_text(stmt, 5, svUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, tlUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, msgUtf8.constData(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<int>::fail(TransitionError::DatabaseError, err);
    }
    const int newId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return Result<int>::success(newId);
}

Result<bool> ActivityStore::dismissNotification(int id) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (id <= 0) return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid notification ID");

    const char* sql = "UPDATE notifications SET is_dismissed = 1, dismissed_at = CURRENT_TIMESTAMP WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    sqlite3_bind_int(stmt, 1, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    sqlite3_finalize(stmt);
    return Result<bool>::success(true);
}

Result<bool> ActivityStore::dismissAll() {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (!m_db.execute("UPDATE notifications SET is_dismissed = 1, dismissed_at = CURRENT_TIMESTAMP "
                      "WHERE is_dismissed = 0;")) {
        return Result<bool>::fail(TransitionError::DatabaseError, "Failed to dismiss notifications");
    }
    return Result<bool>::success(true);
}

QList<ActivityItem> ActivityStore::getActivityFeed(bool includeDismissed) const {
    QList<ActivityItem> feed;
    if (!m_db.handle()) return feed;

    // 1. Notifications
    {
        const char* sql =
            includeDismissed
                ? "SELECT id, target_id, target_type, type, run_id, severity, title, message, is_dismissed, created_at "
                  "FROM notifications ORDER BY id DESC;"
                : "SELECT id, target_id, target_type, type, run_id, severity, title, message, is_dismissed, created_at "
                  "FROM notifications WHERE is_dismissed = 0 ORDER BY id DESC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                ActivityItem item;
                item.id = sqlite3_column_int(stmt, 0);
                item.source = QStringLiteral("notification");
                item.targetId = sqlite3_column_int(stmt, 1);
                item.targetType = colText(stmt, 2);
                item.type = colText(stmt, 3);
                item.runId = sqlite3_column_int(stmt, 4);
                item.severity = colText(stmt, 5);
                if (item.severity.isEmpty()) item.severity = QStringLiteral("info");
                item.title = colText(stmt, 6);
                item.message = colText(stmt, 7);
                item.isDismissed = sqlite3_column_int(stmt, 8) != 0;
                item.createdAt = colText(stmt, 9);
                feed.append(item);
            }
            sqlite3_finalize(stmt);
        }
    }

    // 2. Run-derived activity (running, failed, conflict, completed document review)
    {
        const char* sql =
            "SELECT id, target_kind, target_id, target_action, model, state, last_error, created_at "
            "FROM ai_runs WHERE state IN ('running', 'failed', 'conflict', 'completed') ORDER BY id DESC LIMIT 50;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const int runId = sqlite3_column_int(stmt, 0);
                const QString targetKind = colText(stmt, 1);
                const int targetId = sqlite3_column_int(stmt, 2);
                const QString action = colText(stmt, 3);
                const QString model = colText(stmt, 4);
                const QString state = colText(stmt, 5);
                const QString lastError = colText(stmt, 6);
                const QString createdAt = colText(stmt, 7);

                ActivityItem item;
                item.id = runId;
                item.source = QStringLiteral("run");
                item.runId = runId;
                item.targetType = targetKind;
                item.targetId = targetId;
                item.type = state;
                item.createdAt = createdAt;

                if (state == QLatin1String("failed")) {
                    item.severity = QStringLiteral("error");
                    item.title = QStringLiteral("AI Run Failed (%1)").arg(model);
                    item.message = lastError;
                } else if (state == QLatin1String("conflict")) {
                    item.severity = QStringLiteral("action_required");
                    item.title = QStringLiteral("AI Output Conflict (%1)").arg(targetKind);
                    item.message = lastError;
                } else if (state == QLatin1String("running")) {
                    item.severity = QStringLiteral("info");
                    item.title = QStringLiteral("Running %1 (%2)").arg(action.isEmpty() ? targetKind : action, model);
                } else {
                    item.severity = QStringLiteral("info");
                    item.title = QStringLiteral("Completed %1 (%2)").arg(action.isEmpty() ? targetKind : action, model);
                }
                feed.append(item);
            }
            sqlite3_finalize(stmt);
        }
    }

    std::sort(feed.begin(), feed.end(),
              [](const ActivityItem& a, const ActivityItem& b) { return a.createdAt > b.createdAt; });
    return feed;
}

int ActivityStore::unreadCount() const {
    if (!m_db.handle()) return 0;
    int count = 0;
    m_db.queryInt("SELECT COUNT(*) FROM notifications WHERE is_dismissed = 0;", count);
    return count;
}

}  // namespace store
