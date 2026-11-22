#include "SearchStore.h"

#include <sqlcipher/sqlite3.h>

#include <QStringList>

namespace store {

namespace {

QString colText(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return QString();
    const unsigned char* txt = sqlite3_column_text(stmt, idx);
    return txt ? QString::fromUtf8(reinterpret_cast<const char*>(txt)) : QString();
}

QString itemKindToIndexKey(ItemKind kind) {
    switch (kind) {
        case ItemKind::Folder:
            return QStringLiteral("folder");
        case ItemKind::Document:
            return QStringLiteral("document");
        case ItemKind::Note:
            return QStringLiteral("note");
        case ItemKind::Template:
            return QStringLiteral("template");
        case ItemKind::Draft:
            return QStringLiteral("draft");
        case ItemKind::ChatSession:
            return QStringLiteral("chat_session");
        case ItemKind::ChatNode:
            return QStringLiteral("chat_node");
        case ItemKind::Unknown:
            break;
    }
    return QStringLiteral("unknown");
}

ItemKind indexKeyToItemKind(const QString& key) {
    if (key == QLatin1String("document")) return ItemKind::Document;
    if (key == QLatin1String("note")) return ItemKind::Note;
    if (key == QLatin1String("template")) return ItemKind::Template;
    if (key == QLatin1String("draft")) return ItemKind::Draft;
    if (key == QLatin1String("chat_session")) return ItemKind::ChatSession;
    if (key == QLatin1String("chat_node")) return ItemKind::ChatNode;
    if (key == QLatin1String("folder")) return ItemKind::Folder;
    return ItemKind::Unknown;
}

QString makeSnippet(const QString& body, const QStringList& terms) {
    const QString trimmed = body.simplified();
    if (trimmed.isEmpty()) return QString();

    int firstPos = -1;
    for (const QString& term : terms) {
        const int idx = trimmed.indexOf(term, 0, Qt::CaseInsensitive);
        if (idx >= 0 && (firstPos < 0 || idx < firstPos)) {
            firstPos = idx;
        }
    }
    if (firstPos < 0) return trimmed.left(120);

    const int start = qMax(0, firstPos - 30);
    QString snippet = trimmed.mid(start, 120);
    if (start > 0) snippet.prepend(QStringLiteral("..."));
    if (start + 120 < trimmed.size()) snippet.append(QStringLiteral("..."));
    return snippet;
}

}  // namespace

// ============================================================================
// Constructor
// ============================================================================

SearchStore::SearchStore(db::Database& db) : m_db(db) {}

// ============================================================================
// Public Methods
// ============================================================================

Result<bool> SearchStore::upsertItem(const ItemRef& item, int folderId, const QString& title, const QString& body) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (!item.isValid() || item.id <= 0) {
        return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid ItemRef for search index");
    }

    const char* sql =
        "INSERT INTO search_index (item_kind, item_id, folder_id, title, body, updated_at) "
        "VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP) "
        "ON CONFLICT(item_kind, item_id) DO UPDATE SET "
        "folder_id = excluded.folder_id, title = excluded.title, body = excluded.body, "
        "updated_at = CURRENT_TIMESTAMP;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }

    const QByteArray kindUtf8 = itemKindToIndexKey(item.kind).toUtf8();
    const QByteArray titleUtf8 = title.toUtf8();
    const QByteArray bodyUtf8 = body.toUtf8();
    sqlite3_bind_text(stmt, 1, kindUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, item.id);
    sqlite3_bind_int(stmt, 3, folderId);
    sqlite3_bind_text(stmt, 4, titleUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, bodyUtf8.constData(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    sqlite3_finalize(stmt);
    return Result<bool>::success(true);
}

Result<bool> SearchStore::removeItem(const ItemRef& item) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (!item.isValid() || item.id <= 0) {
        return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid ItemRef");
    }

    const char* sql = "DELETE FROM search_index WHERE item_kind = ? AND item_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    const QByteArray kindUtf8 = itemKindToIndexKey(item.kind).toUtf8();
    sqlite3_bind_text(stmt, 1, kindUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, item.id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    sqlite3_finalize(stmt);
    return Result<bool>::success(true);
}

Result<int> SearchStore::rebuildIndex() {
    if (!m_db.handle()) return Result<int>::fail(TransitionError::DatabaseError, "Database not open");
    if (!m_db.execute("BEGIN IMMEDIATE;")) {
        return Result<int>::fail(TransitionError::DatabaseError, "Failed to start rebuildIndex transaction");
    }
    auto rollback = [this](const QString& msg) {
        m_db.execute("ROLLBACK;");
        return Result<int>::fail(TransitionError::DatabaseError, msg);
    };

    if (!m_db.execute("DELETE FROM search_index;")) {
        return rollback("Failed to clear search_index");
    }

    int indexed = 0;

    // 1. Index artifacts (documents, notes, templates, drafts)
    {
        const char* sql =
            "SELECT a.kind, a.id, a.folder_id, COALESCE(v.title, ''), COALESCE(v.content, '') "
            "FROM artifacts a LEFT JOIN artifact_versions v ON v.id = a.current_version_id;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return rollback(QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const QString k = colText(stmt, 0);
            const int id = sqlite3_column_int(stmt, 1);
            const int folderId = sqlite3_column_int(stmt, 2);
            const QString title = colText(stmt, 3);
            const QString body = colText(stmt, 4);

            const ItemRef ref{indexKeyToItemKind(k), id, k};
            if (upsertItem(ref, folderId, title, body).isSuccess()) {
                ++indexed;
            }
        }
        sqlite3_finalize(stmt);
    }

    // 2. Index chat sessions + concatenated messages
    {
        const char* sql =
            "SELECT c.id, c.folder_id, c.title, "
            "COALESCE((SELECT GROUP_CONCAT(m.content, '\n') FROM messages m WHERE m.chat_id = c.id), '') "
            "FROM chat_sessions c;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return rollback(QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const int chatId = sqlite3_column_int(stmt, 0);
            const int folderId = sqlite3_column_int(stmt, 1);
            const QString title = colText(stmt, 2);
            const QString body = colText(stmt, 3);

            const ItemRef ref{ItemKind::ChatSession, chatId, QStringLiteral("chats")};
            if (upsertItem(ref, folderId, title, body).isSuccess()) {
                ++indexed;
            }
        }
        sqlite3_finalize(stmt);
    }

    if (!m_db.execute("INSERT INTO search_fts(search_fts) VALUES('rebuild');")) {
        return rollback("Failed to rebuild FTS5 index");
    }

    if (!m_db.execute("COMMIT;")) {
        return rollback("Failed to commit rebuildIndex");
    }
    return Result<int>::success(indexed);
}

QList<SearchHit> SearchStore::search(const QString& query, std::optional<ItemKind> kindFilter, int limit) const {
    QList<SearchHit> hits;
    if (!m_db.handle() || query.trimmed().isEmpty() || limit <= 0) return hits;

    const QStringList rawTerms = query.trimmed().split(' ', Qt::SkipEmptyParts);
    QStringList ftsClauses;
    for (const QString& raw : rawTerms) {
        QString cleaned = raw;
        cleaned.remove(QLatin1Char('"'));
        if (cleaned.isEmpty()) continue;
        ftsClauses.append(QStringLiteral("\"%1\"*").arg(cleaned));
    }
    if (ftsClauses.isEmpty()) return hits;
    const QString ftsQuery = ftsClauses.join(QLatin1Char(' '));

    QString sql =
        "SELECT s.item_kind, s.item_id, s.folder_id, s.title, "
        "COALESCE(NULLIF(snippet(search_fts, 1, '[', ']', '...', 24), ''), "
        "         NULLIF(snippet(search_fts, 0, '[', ']', '...', 16), ''), s.body), "
        "s.updated_at "
        "FROM search_fts "
        "JOIN search_index s ON s.id = search_fts.rowid "
        "WHERE search_fts MATCH ?";
    if (kindFilter.has_value()) {
        sql += " AND s.item_kind = ?";
    }
    sql += " ORDER BY bm25(search_fts, 2.0, 1.0), s.updated_at DESC, s.id DESC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return hits;
    }

    int bindIdx = 1;
    const QByteArray ftsUtf8 = ftsQuery.toUtf8();
    sqlite3_bind_text(stmt, bindIdx++, ftsUtf8.constData(), -1, SQLITE_TRANSIENT);
    QByteArray kindUtf8;
    if (kindFilter.has_value()) {
        kindUtf8 = itemKindToIndexKey(*kindFilter).toUtf8();
        sqlite3_bind_text(stmt, bindIdx++, kindUtf8.constData(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(stmt, bindIdx, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const QString kindStr = colText(stmt, 0);
        const int itemId = sqlite3_column_int(stmt, 1);
        const int folderId = sqlite3_column_int(stmt, 2);
        const QString title = colText(stmt, 3);
        const QString snippetText = colText(stmt, 4);
        const QString updatedAt = colText(stmt, 5);

        SearchHit hit;
        hit.item = {indexKeyToItemKind(kindStr), itemId, kindStr};
        hit.title = title;
        hit.snippet = snippetText.isEmpty() ? makeSnippet(title, rawTerms) : snippetText;
        hit.folderId = folderId;
        hit.updatedAt = updatedAt;
        hits.append(hit);
    }
    sqlite3_finalize(stmt);
    return hits;
}

}  // namespace store
