#include "ArtifactStore.h"

#include <sqlcipher/sqlite3.h>

#include "../db/Database.h"
#include "../db/Transaction.h"

namespace store {

ArtifactStore::ArtifactStore(db::Database& db) : m_db(db) {}

QString kindToString(ArtifactKind kind) {
    switch (kind) {
        case ArtifactKind::Document:
            return "document";
        case ArtifactKind::Note:
            return "note";
        case ArtifactKind::Template:
            return "template";
    }
    return "unknown";
}

std::optional<ArtifactKind> stringToKind(const QString& kindStr) {
    if (kindStr == "document") return ArtifactKind::Document;
    if (kindStr == "note") return ArtifactKind::Note;
    if (kindStr == "template") return ArtifactKind::Template;
    return std::nullopt;
}

std::optional<Artifact> ArtifactStore::getArtifact(int id) const {
    QString sql = "SELECT id, kind, folder_id, current_version_id, created_at FROM artifacts WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db.handle(), sql.toUtf8().constData(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);

    std::optional<Artifact> result = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Artifact artifact;
        artifact.id = sqlite3_column_int(stmt, 0);

        QString kindStr = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        auto kind = stringToKind(kindStr);
        if (kind) {
            artifact.kind = *kind;
            artifact.folderId = sqlite3_column_int(stmt, 2);
            artifact.currentVersionId = sqlite3_column_int(stmt, 3);
            artifact.createdAt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
            result = artifact;
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<ArtifactVersion> ArtifactStore::getVersion(int id) const {
    QString sql =
        "SELECT id, artifact_id, parent_id, forked_from_version_id, title, content, metadata, is_sealed, created_at "
        "FROM artifact_versions WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db.handle(), sql.toUtf8().constData(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);

    std::optional<ArtifactVersion> result = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        ArtifactVersion version;
        version.id = sqlite3_column_int(stmt, 0);
        version.artifactId = sqlite3_column_int(stmt, 1);

        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            version.parentId = sqlite3_column_int(stmt, 2);
        }

        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) {
            version.forkedFromVersionId = sqlite3_column_int(stmt, 3);
        }

        const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (title) version.title = QString::fromUtf8(title);

        const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (content) version.content = QString::fromUtf8(content);

        const char* metadata = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (metadata) version.metadata = QString::fromUtf8(metadata);

        version.isSealed = sqlite3_column_int(stmt, 7) != 0;

        const char* createdAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        if (createdAt) version.createdAt = QString::fromUtf8(createdAt);

        result = version;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<ArtifactVersion> ArtifactStore::getCurrentVersion(int artifactId) const {
    auto artifact = getArtifact(artifactId);
    if (!artifact) return std::nullopt;
    return getVersion(artifact->currentVersionId);
}

Result<ArtifactVersion> ArtifactStore::createArtifact(ArtifactKind kind, int folderId, const QString& title,
                                                      const QString& content, const QString& metadata) {
    db::Transaction tx(m_db);

    QString insertArtifactSql = "INSERT INTO artifacts (kind, folder_id) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), insertArtifactSql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to prepare artifact insert");
    }

    sqlite3_bind_text(stmt, 1, kindToString(kind).toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, folderId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to insert artifact");
    }

    int artifactId = sqlite3_last_insert_rowid(m_db.handle());
    sqlite3_finalize(stmt);

    QString insertVersionSql =
        "INSERT INTO artifact_versions (artifact_id, parent_id, forked_from_version_id, title, content, metadata, "
        "is_sealed) VALUES (?, NULL, NULL, ?, ?, ?, 0)";
    if (sqlite3_prepare_v2(m_db.handle(), insertVersionSql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to prepare version insert");
    }

    sqlite3_bind_int(stmt, 1, artifactId);
    sqlite3_bind_text(stmt, 2, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, metadata.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to insert version");
    }

    int versionId = sqlite3_last_insert_rowid(m_db.handle());
    sqlite3_finalize(stmt);

    QString updateArtifactSql = "UPDATE artifacts SET current_version_id = ? WHERE id = ?";
    if (sqlite3_prepare_v2(m_db.handle(), updateArtifactSql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to prepare artifact update");
    }

    sqlite3_bind_int(stmt, 1, versionId);
    sqlite3_bind_int(stmt, 2, artifactId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError,
                                             "Failed to update artifact current version");
    }
    sqlite3_finalize(stmt);

    if (!tx.commit()) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Transaction commit failed");
    }

    auto version = getVersion(versionId);
    if (version) {
        return Result<ArtifactVersion>::success(*version);
    }
    return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Failed to retrieve created version");
}

Result<ArtifactVersion> ArtifactStore::editVersion(int expectedVersionId, const QString& title, const QString& content,
                                                   const QString& metadata) {
    db::Transaction tx(m_db);

    auto existingVersion = getVersion(expectedVersionId);
    if (!existingVersion) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Version not found");
    }

    if (existingVersion->isSealed) {
        return Result<ArtifactVersion>::fail(TransitionError::Sealed, "Cannot edit a sealed version");
    }

    auto artifact = getArtifact(existingVersion->artifactId);
    if (!artifact) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Artifact not found");
    }

    if (artifact->currentVersionId != expectedVersionId) {
        return Result<ArtifactVersion>::fail(TransitionError::Conflict,
                                             "Version is not the current version of the artifact");
    }

    QString updateSql = "UPDATE artifact_versions SET title = ?, content = ?, metadata = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), updateSql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to prepare version update");
    }

    sqlite3_bind_text(stmt, 1, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, metadata.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, expectedVersionId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to update version");
    }
    sqlite3_finalize(stmt);

    if (!tx.commit()) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Transaction commit failed");
    }

    auto version = getVersion(expectedVersionId);
    if (version) {
        return Result<ArtifactVersion>::success(*version);
    }
    return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Failed to retrieve updated version");
}

Result<ArtifactVersion> ArtifactStore::sealVersion(int versionId) {
    db::Transaction tx(m_db);

    auto existingVersion = getVersion(versionId);
    if (!existingVersion) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Version not found");
    }

    if (existingVersion->isSealed) {
        return Result<ArtifactVersion>::success(*existingVersion);
    }

    QString updateSql = "UPDATE artifact_versions SET is_sealed = 1 WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), updateSql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to prepare version seal update");
    }

    sqlite3_bind_int(stmt, 1, versionId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to seal version");
    }
    sqlite3_finalize(stmt);

    if (!tx.commit()) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Transaction commit failed");
    }

    auto version = getVersion(versionId);
    if (version) {
        return Result<ArtifactVersion>::success(*version);
    }
    return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Failed to retrieve sealed version");
}

Result<ArtifactVersion> ArtifactStore::createMutableDescendant(int expectedBaseVersionId, const QString& title,
                                                               const QString& content, const QString& metadata) {
    db::Transaction tx(m_db);

    auto baseVersion = getVersion(expectedBaseVersionId);
    if (!baseVersion) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Base version not found");
    }

    if (!baseVersion->isSealed) {
        return Result<ArtifactVersion>::fail(TransitionError::Conflict,
                                             "Base version must be sealed to create a descendant");
    }

    auto artifact = getArtifact(baseVersion->artifactId);
    if (!artifact) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Artifact not found");
    }

    if (artifact->currentVersionId != expectedBaseVersionId) {
        return Result<ArtifactVersion>::fail(TransitionError::Conflict,
                                             "Base version is not the current version of the artifact");
    }

    QString insertVersionSql =
        "INSERT INTO artifact_versions (artifact_id, parent_id, forked_from_version_id, title, content, metadata, "
        "is_sealed) VALUES (?, ?, NULL, ?, ?, ?, 0)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), insertVersionSql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError,
                                             "Failed to prepare descendant version insert");
    }

    sqlite3_bind_int(stmt, 1, baseVersion->artifactId);
    sqlite3_bind_int(stmt, 2, expectedBaseVersionId);
    sqlite3_bind_text(stmt, 3, title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, metadata.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to insert descendant version");
    }

    int newVersionId = sqlite3_last_insert_rowid(m_db.handle());
    sqlite3_finalize(stmt);

    QString updateArtifactSql = "UPDATE artifacts SET current_version_id = ? WHERE id = ?";
    if (sqlite3_prepare_v2(m_db.handle(), updateArtifactSql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to prepare artifact update");
    }

    sqlite3_bind_int(stmt, 1, newVersionId);
    sqlite3_bind_int(stmt, 2, baseVersion->artifactId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError,
                                             "Failed to update artifact current version");
    }
    sqlite3_finalize(stmt);

    if (!tx.commit()) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Transaction commit failed");
    }

    auto version = getVersion(newVersionId);
    if (version) {
        return Result<ArtifactVersion>::success(*version);
    }
    return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Failed to retrieve created descendant version");
}

Result<ArtifactVersion> ArtifactStore::restoreVersion(int expectedBaseVersionId, int versionToRestoreId) {
    auto versionToRestore = getVersion(versionToRestoreId);
    if (!versionToRestore) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Version to restore not found");
    }

    auto baseVersion = getVersion(expectedBaseVersionId);
    if (!baseVersion) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Base version not found");
    }

    if (versionToRestore->artifactId != baseVersion->artifactId) {
        return Result<ArtifactVersion>::fail(TransitionError::InvalidCrossReference,
                                             "Cannot restore version from a different artifact");
    }

    return createMutableDescendant(expectedBaseVersionId, versionToRestore->title, versionToRestore->content,
                                   versionToRestore->metadata);
}

Result<ArtifactVersion> ArtifactStore::forkArtifact(int expectedBaseVersionId, int folderId) {
    db::Transaction tx(m_db);

    auto baseVersion = getVersion(expectedBaseVersionId);
    if (!baseVersion) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Base version not found");
    }

    if (!baseVersion->isSealed) {
        return Result<ArtifactVersion>::fail(TransitionError::Conflict, "Base version must be sealed to fork");
    }

    auto baseArtifact = getArtifact(baseVersion->artifactId);
    if (!baseArtifact) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Base artifact not found");
    }

    QString insertArtifactSql = "INSERT INTO artifacts (kind, folder_id) VALUES (?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), insertArtifactSql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError,
                                             "Failed to prepare forked artifact insert");
    }

    sqlite3_bind_text(stmt, 1, kindToString(baseArtifact->kind).toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, folderId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to insert forked artifact");
    }

    int newArtifactId = sqlite3_last_insert_rowid(m_db.handle());
    sqlite3_finalize(stmt);

    QString insertVersionSql =
        "INSERT INTO artifact_versions (artifact_id, parent_id, forked_from_version_id, title, content, metadata, "
        "is_sealed) VALUES (?, NULL, ?, ?, ?, ?, 0)";
    if (sqlite3_prepare_v2(m_db.handle(), insertVersionSql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to prepare version insert");
    }

    sqlite3_bind_int(stmt, 1, newArtifactId);
    sqlite3_bind_int(stmt, 2, expectedBaseVersionId);
    sqlite3_bind_text(stmt, 3, baseVersion->title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, baseVersion->content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, baseVersion->metadata.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to insert version");
    }

    int newVersionId = sqlite3_last_insert_rowid(m_db.handle());
    sqlite3_finalize(stmt);

    QString updateArtifactSql = "UPDATE artifacts SET current_version_id = ? WHERE id = ?";
    if (sqlite3_prepare_v2(m_db.handle(), updateArtifactSql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to prepare artifact update");
    }

    sqlite3_bind_int(stmt, 1, newVersionId);
    sqlite3_bind_int(stmt, 2, newArtifactId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError,
                                             "Failed to update artifact current version");
    }
    sqlite3_finalize(stmt);

    if (!tx.commit()) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Transaction commit failed for fork");
    }

    auto version = getVersion(newVersionId);
    if (version) {
        return Result<ArtifactVersion>::success(*version);
    }
    return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Failed to retrieve forked version");
}

}  // namespace store
