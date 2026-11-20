#include "FolderStore.h"

#include <sqlcipher/sqlite3.h>

#include <QSet>

namespace store {

// ============================================================================
// Constructor
// ============================================================================

FolderStore::FolderStore(db::Database& db) : m_db(db) {}

// ============================================================================
// Public Methods
// ============================================================================

Result<int> FolderStore::createFolder(int parentId, const QString& name, const QString& rootType, bool isExpanded) {
    if (!m_db.handle()) return Result<int>::fail(TransitionError::DatabaseError, "Database not open");
    if (name.trimmed().isEmpty()) {
        return Result<int>::fail(TransitionError::InvalidCrossReference, "Folder name cannot be empty");
    }
    if (parentId < 0) {
        return Result<int>::fail(TransitionError::InvalidCrossReference, "Parent folder ID cannot be negative");
    }

    if (parentId > 0 && !getFolder(parentId).has_value()) {
        return Result<int>::fail(TransitionError::NotFound, "Parent folder does not exist");
    }

    const QString normalizedType = normalizeFolderRootType(rootType);
    const char* sql = "INSERT INTO folders (parent_id, name, type, is_expanded) VALUES (?, ?, ?, ?) RETURNING id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<int>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }

    const QByteArray nameUtf8 = name.toUtf8();
    const QByteArray typeUtf8 = normalizedType.toUtf8();
    sqlite3_bind_int(stmt, 1, parentId);
    sqlite3_bind_text(stmt, 2, nameUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, typeUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, isExpanded ? 1 : 0);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<int>::fail(TransitionError::DatabaseError, err);
    }

    const int newId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return Result<int>::success(newId);
}

std::optional<FolderRecord> FolderStore::getFolder(int id) const {
    if (!m_db.handle() || id <= 0) return std::nullopt;

    const char* sql = "SELECT id, parent_id, name, type, timestamp, position, is_expanded FROM folders WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);
    std::optional<FolderRecord> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        FolderRecord rec;
        rec.id = sqlite3_column_int(stmt, 0);
        rec.parentId = sqlite3_column_int(stmt, 1);
        const char* nameText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* typeText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* tsText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        rec.name = nameText ? QString::fromUtf8(nameText) : QString();
        rec.rootType = normalizeFolderRootType(typeText ? QString::fromUtf8(typeText) : QString());
        rec.timestamp = QDateTime::fromString(tsText ? QString::fromUtf8(tsText) : QString(), Qt::ISODate);
        rec.position = sqlite3_column_int(stmt, 5);
        rec.isExpanded = sqlite3_column_int(stmt, 6) != 0;
        out = rec;
    }
    sqlite3_finalize(stmt);
    return out;
}

Result<QList<FolderRecord>> FolderStore::getFolders(const QString& rootType) const {
    if (!m_db.handle()) return Result<QList<FolderRecord>>::fail(TransitionError::DatabaseError, "Database not open");

    const bool fetchAll = rootType.trimmed().isEmpty() || rootType.trimmed().toLower() == "unified";
    const QString normalizedType = fetchAll ? QString() : normalizeFolderRootType(rootType);

    const char* sql = fetchAll ? "SELECT id, parent_id, name, type, timestamp, position, is_expanded FROM folders "
                                 "ORDER BY position ASC, name ASC;"
                               : "SELECT id, parent_id, name, type, timestamp, position, is_expanded FROM folders "
                                 "WHERE type = ? OR (? = 'documents' AND (type = 'folder' OR type = '' OR type IS "
                                 "NULL)) ORDER BY position ASC, name ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<QList<FolderRecord>>::fail(TransitionError::DatabaseError,
                                                 QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }

    const QByteArray typeUtf8 = normalizedType.toUtf8();
    if (!fetchAll) {
        sqlite3_bind_text(stmt, 1, typeUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, typeUtf8.constData(), -1, SQLITE_TRANSIENT);
    }

    QList<FolderRecord> list;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FolderRecord rec;
        rec.id = sqlite3_column_int(stmt, 0);
        rec.parentId = sqlite3_column_int(stmt, 1);
        const char* nameText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* typeText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* tsText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        rec.name = nameText ? QString::fromUtf8(nameText) : QString();
        rec.rootType = normalizeFolderRootType(typeText ? QString::fromUtf8(typeText) : QString());
        rec.timestamp = QDateTime::fromString(tsText ? QString::fromUtf8(tsText) : QString(), Qt::ISODate);
        rec.position = sqlite3_column_int(stmt, 5);
        rec.isExpanded = sqlite3_column_int(stmt, 6) != 0;
        list.append(rec);
    }
    sqlite3_finalize(stmt);
    return Result<QList<FolderRecord>>::success(list);
}

Result<bool> FolderStore::renameFolder(int id, const QString& newName) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (id <= 0) return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid folder ID");
    if (newName.trimmed().isEmpty()) {
        return Result<bool>::fail(TransitionError::InvalidCrossReference, "Folder name cannot be empty");
    }

    const char* sql = "UPDATE folders SET name = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }

    const QByteArray nameUtf8 = newName.toUtf8();
    sqlite3_bind_text(stmt, 1, nameUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    const int changes = sqlite3_changes(m_db.handle());
    sqlite3_finalize(stmt);
    if (changes == 0) return Result<bool>::fail(TransitionError::NotFound, "Folder not found");
    return Result<bool>::success(true);
}

Result<bool> FolderStore::setExpanded(int id, bool expanded) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (id <= 0) return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid folder ID");

    const char* sql = "UPDATE folders SET is_expanded = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    sqlite3_bind_int(stmt, 1, expanded ? 1 : 0);
    sqlite3_bind_int(stmt, 2, id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    sqlite3_finalize(stmt);
    return Result<bool>::success(true);
}

bool FolderStore::wouldCreateCycle(int folderId, int candidateParentId) const {
    if (folderId <= 0 || candidateParentId <= 0) return false;
    if (folderId == candidateParentId) return true;

    QSet<int> visited;
    visited.insert(folderId);

    int curr = candidateParentId;
    while (curr > 0) {
        if (visited.contains(curr)) return true;
        visited.insert(curr);

        auto folderOpt = getFolder(curr);
        if (!folderOpt.has_value()) break;
        curr = folderOpt->parentId;
    }
    return false;
}

Result<bool> FolderStore::moveFolder(int folderId, int newParentId) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (folderId <= 0 || newParentId < 0) {
        return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid folder IDs for moveFolder");
    }
    if (!getFolder(folderId).has_value()) {
        return Result<bool>::fail(TransitionError::NotFound, "Source folder not found");
    }
    if (newParentId > 0 && !getFolder(newParentId).has_value()) {
        return Result<bool>::fail(TransitionError::NotFound, "Target parent folder not found");
    }
    if (wouldCreateCycle(folderId, newParentId)) {
        return Result<bool>::fail(TransitionError::Conflict,
                                  "Cannot move a folder into itself or one of its descendants");
    }

    const char* sql = "UPDATE folders SET parent_id = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    sqlite3_bind_int(stmt, 1, newParentId);
    sqlite3_bind_int(stmt, 2, folderId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    sqlite3_finalize(stmt);
    return Result<bool>::success(true);
}

Result<bool> FolderStore::moveItem(const ItemRef& item, int targetFolderId) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (!item.isValid() || item.id <= 0 || targetFolderId < 0) {
        return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid item reference or target folder ID");
    }
    if (targetFolderId > 0 && !getFolder(targetFolderId).has_value()) {
        return Result<bool>::fail(TransitionError::NotFound, "Target folder does not exist");
    }

    if (item.kind == ItemKind::Folder) {
        return moveFolder(item.id, targetFolderId);
    }

    if (auto artKindOpt = toArtifactKind(item.kind)) {
        auto artIdOpt = resolveArtifactId(*artKindOpt, item.id);
        if (!artIdOpt.has_value()) {
            return Result<bool>::fail(TransitionError::NotFound, "Artifact not found for moveItem");
        }
        ArtifactStore artStore(m_db);
        auto res = artStore.moveArtifact(*artIdOpt, targetFolderId);
        if (!res.isSuccess()) {
            return Result<bool>::fail(res.error.value_or(TransitionError::DatabaseError), res.errorMessage);
        }
        return Result<bool>::success(true);
    }

    if (item.kind == ItemKind::ChatSession || item.kind == ItemKind::ChatNode) {
        const char* sql = "UPDATE messages SET folder_id = ? WHERE id = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
        }
        sqlite3_bind_int(stmt, 1, targetFolderId);
        sqlite3_bind_int(stmt, 2, item.id);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
            sqlite3_finalize(stmt);
            return Result<bool>::fail(TransitionError::DatabaseError, err);
        }
        const int changes = sqlite3_changes(m_db.handle());
        sqlite3_finalize(stmt);
        if (changes == 0) return Result<bool>::fail(TransitionError::NotFound, "Chat message not found");
        return Result<bool>::success(true);
    }

    return Result<bool>::fail(TransitionError::UnsupportedOperation, "Unsupported item kind for moveItem");
}

Result<bool> FolderStore::deleteFolder(int id) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (id <= 0) return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid folder ID");

    auto folderOpt = getFolder(id);
    if (!folderOpt.has_value()) return Result<bool>::fail(TransitionError::NotFound, "Folder not found");
    const int fallbackParentId = folderOpt->parentId;

    if (!m_db.execute("BEGIN IMMEDIATE;")) {
        return Result<bool>::fail(TransitionError::DatabaseError, "Failed to start transaction for deleteFolder");
    }

    auto rollbackAndFail = [this](const QString& msg) {
        m_db.execute("ROLLBACK;");
        return Result<bool>::fail(TransitionError::DatabaseError, msg);
    };

    if (!m_db.execute(
            QString("UPDATE folders SET parent_id = %1 WHERE parent_id = %2;").arg(fallbackParentId).arg(id))) {
        return rollbackAndFail("Failed to re-parent child folders");
    }
    if (!m_db.execute(
            QString("UPDATE artifacts SET folder_id = %1 WHERE folder_id = %2;").arg(fallbackParentId).arg(id))) {
        return rollbackAndFail("Failed to re-parent folder artifacts");
    }
    if (!m_db.execute(
            QString("UPDATE messages SET folder_id = %1 WHERE folder_id = %2;").arg(fallbackParentId).arg(id))) {
        return rollbackAndFail("Failed to re-parent folder messages");
    }
    if (!m_db.execute(QString("DELETE FROM folders WHERE id = %1;").arg(id))) {
        return rollbackAndFail("Failed to delete folder");
    }

    if (!m_db.execute("COMMIT;")) {
        return rollbackAndFail("Failed to commit deleteFolder transaction");
    }
    return Result<bool>::success(true);
}

// ============================================================================
// Private Helpers
// ============================================================================

std::optional<int> FolderStore::resolveArtifactId(ArtifactKind kind, int idOrLegacyId) const {
    if (!m_db.handle() || idOrLegacyId <= 0) return std::nullopt;

    const char* kindStr = "document";
    const char* mapTable = "legacy_document_mapping";
    const char* mapCol = "legacy_document_id";
    switch (kind) {
        case ArtifactKind::Document:
            kindStr = "document";
            mapTable = "legacy_document_mapping";
            mapCol = "legacy_document_id";
            break;
        case ArtifactKind::Note:
            kindStr = "note";
            mapTable = "legacy_note_mapping";
            mapCol = "legacy_note_id";
            break;
        case ArtifactKind::Template:
            kindStr = "template";
            mapTable = "legacy_template_mapping";
            mapCol = "legacy_template_id";
            break;
        case ArtifactKind::Draft:
            kindStr = "draft";
            mapTable = "legacy_draft_mapping";
            mapCol = "legacy_draft_id";
            break;
    }

    {
        const QString mapSql = QString("SELECT artifact_id FROM %1 WHERE %2 = ?;").arg(mapTable, mapCol);
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db.handle(), mapSql.toUtf8().constData(), -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, idOrLegacyId);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const int artId = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
                return artId;
            }
            sqlite3_finalize(stmt);
        }
    }

    {
        const char* directSql = "SELECT id FROM artifacts WHERE id = ? AND kind = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db.handle(), directSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, idOrLegacyId);
            sqlite3_bind_text(stmt, 2, kindStr, -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const int artId = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
                return artId;
            }
            sqlite3_finalize(stmt);
        }
    }

    return std::nullopt;
}

}  // namespace store
