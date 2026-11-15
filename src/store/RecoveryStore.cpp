#include "RecoveryStore.h"

#include <sqlcipher/sqlite3.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "../db/Database.h"
#include "../db/Transaction.h"
#include "ArtifactStore.h"

namespace store {

namespace {

QString columnTextOrNull(sqlite3_stmt* stmt, int index) {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return QString();
    }
    const unsigned char* text = sqlite3_column_text(stmt, index);
    return text ? QString::fromUtf8(reinterpret_cast<const char*>(text)) : QString();
}

std::optional<int> columnOptionalInt(sqlite3_stmt* stmt, int index) {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int(stmt, index);
}

QuarantinedRecord readRow(sqlite3_stmt* stmt) {
    QuarantinedRecord rec;
    rec.id = sqlite3_column_int(stmt, 0);
    rec.sourceTable = columnTextOrNull(stmt, 1);
    rec.legacyId = columnOptionalInt(stmt, 2);
    rec.targetType = columnTextOrNull(stmt, 3);
    rec.parentReference = columnOptionalInt(stmt, 4);
    rec.title = columnTextOrNull(stmt, 5);
    rec.content = columnTextOrNull(stmt, 6);
    rec.rawPayloadJson = columnTextOrNull(stmt, 7);
    rec.reason = columnTextOrNull(stmt, 8);
    rec.status = columnTextOrNull(stmt, 9);
    rec.resolvedTargetKind = columnTextOrNull(stmt, 10);
    rec.resolvedTargetId = columnOptionalInt(stmt, 11);
    rec.createdAt = columnTextOrNull(stmt, 12);
    rec.resolvedAt = columnTextOrNull(stmt, 13);
    return rec;
}

}  // namespace

RecoveryStore::RecoveryStore(db::Database& db) : m_db(db) {}

std::optional<int> RecoveryStore::addQuarantinedRecord(const QString& sourceTable, std::optional<int> legacyId,
                                                       const QString& targetType, std::optional<int> parentReference,
                                                       const QString& title, const QString& content,
                                                       const QString& rawPayloadJson, const QString& reason,
                                                       QString* error) {
    if (sourceTable.trimmed().isEmpty()) {
        if (error) *error = "source_table cannot be empty";
        return std::nullopt;
    }
    if (rawPayloadJson.trimmed().isEmpty()) {
        if (error) *error = "raw_payload_json cannot be empty";
        return std::nullopt;
    }
    if (reason.trimmed().isEmpty()) {
        if (error) *error = "reason cannot be empty";
        return std::nullopt;
    }

    db::Transaction tx(m_db);
    if (tx.isFailed()) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return std::nullopt;
    }

    const char* sql =
        "INSERT INTO quarantined_records (source_table, legacy_id, target_type, parent_reference, title, content, "
        "raw_payload_json, reason, status) VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'quarantined');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return std::nullopt;
    }

    const QByteArray sourceUtf8 = sourceTable.toUtf8();
    const QByteArray targetUtf8 = targetType.toUtf8();
    const QByteArray titleUtf8 = title.toUtf8();
    const QByteArray contentUtf8 = content.toUtf8();
    const QByteArray payloadUtf8 = rawPayloadJson.toUtf8();
    const QByteArray reasonUtf8 = reason.toUtf8();

    sqlite3_bind_text(stmt, 1, sourceUtf8.constData(), -1, SQLITE_TRANSIENT);
    if (legacyId.has_value()) {
        sqlite3_bind_int(stmt, 2, *legacyId);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    sqlite3_bind_text(stmt, 3, targetUtf8.constData(), -1, SQLITE_TRANSIENT);
    if (parentReference.has_value()) {
        sqlite3_bind_int(stmt, 4, *parentReference);
    } else {
        sqlite3_bind_null(stmt, 4);
    }
    if (title.isNull()) {
        sqlite3_bind_null(stmt, 5);
    } else {
        sqlite3_bind_text(stmt, 5, titleUtf8.constData(), -1, SQLITE_TRANSIENT);
    }
    if (content.isNull()) {
        sqlite3_bind_null(stmt, 6);
    } else {
        sqlite3_bind_text(stmt, 6, contentUtf8.constData(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_text(stmt, 7, payloadUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, reasonUtf8.constData(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return std::nullopt;
    }
    sqlite3_finalize(stmt);

    int newId = static_cast<int>(sqlite3_last_insert_rowid(m_db.handle()));
    if (!tx.commit()) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return std::nullopt;
    }
    return newId;
}

std::optional<QuarantinedRecord> RecoveryStore::getRecord(int id, QString* error) const {
    const char* sql =
        "SELECT id, source_table, legacy_id, target_type, parent_reference, title, content, raw_payload_json, reason, "
        "status, resolved_target_kind, resolved_target_id, created_at, resolved_at "
        "FROM quarantined_records WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return std::nullopt;
    }

    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        QuarantinedRecord rec = readRow(stmt);
        sqlite3_finalize(stmt);
        return rec;
    }
    if (rc != SQLITE_DONE && error) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
    }
    sqlite3_finalize(stmt);
    return std::nullopt;
}

QVector<QuarantinedRecord> RecoveryStore::getRecords(const QString& statusFilter, const QString& sourceTableFilter,
                                                     QString* error) const {
    QVector<QuarantinedRecord> result;
    QString sql =
        "SELECT id, source_table, legacy_id, target_type, parent_reference, title, content, raw_payload_json, reason, "
        "status, resolved_target_kind, resolved_target_id, created_at, resolved_at "
        "FROM quarantined_records WHERE 1 = 1";
    if (!statusFilter.isEmpty()) {
        sql += " AND status = ?";
    }
    if (!sourceTableFilter.isEmpty()) {
        sql += " AND source_table = ?";
    }
    sql += " ORDER BY id ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return result;
    }

    int bindIdx = 1;
    const QByteArray statusUtf8 = statusFilter.toUtf8();
    const QByteArray sourceUtf8 = sourceTableFilter.toUtf8();
    if (!statusFilter.isEmpty()) {
        sqlite3_bind_text(stmt, bindIdx++, statusUtf8.constData(), -1, SQLITE_TRANSIENT);
    }
    if (!sourceTableFilter.isEmpty()) {
        sqlite3_bind_text(stmt, bindIdx++, sourceUtf8.constData(), -1, SQLITE_TRANSIENT);
    }

    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        result.append(readRow(stmt));
    }
    if (rc != SQLITE_DONE && error) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
    }
    sqlite3_finalize(stmt);
    return result;
}

int RecoveryStore::getQuarantinedCount(QString* error) const {
    int count = 0;
    if (!m_db.queryInt("SELECT COUNT(*) FROM quarantined_records WHERE status = 'quarantined';", count, error)) {
        return 0;
    }
    return count;
}

QByteArray RecoveryStore::exportRecordsToJson(const QVector<int>& ids, QString* error) const {
    QVector<QuarantinedRecord> records;
    if (ids.isEmpty()) {
        records = getRecords(QString(), QString(), error);
    } else {
        for (int id : ids) {
            auto opt = getRecord(id, error);
            if (!opt.has_value()) {
                if (error && error->isEmpty()) {
                    *error = QString("Quarantined record %1 not found").arg(id);
                }
                return QByteArray();
            }
            records.append(*opt);
        }
    }

    QJsonArray array;
    for (const QuarantinedRecord& rec : records) {
        QJsonObject obj;
        obj["id"] = rec.id;
        obj["source_table"] = rec.sourceTable;
        obj["legacy_id"] = rec.legacyId.has_value() ? QJsonValue(*rec.legacyId) : QJsonValue(QJsonValue::Null);
        obj["target_type"] = rec.targetType;
        obj["parent_reference"] =
            rec.parentReference.has_value() ? QJsonValue(*rec.parentReference) : QJsonValue(QJsonValue::Null);
        obj["title"] = rec.title.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(rec.title);
        obj["content"] = rec.content.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(rec.content);

        QJsonParseError parseErr;
        QJsonDocument rawDoc = QJsonDocument::fromJson(rec.rawPayloadJson.toUtf8(), &parseErr);
        if (parseErr.error == QJsonParseError::NoError && rawDoc.isObject()) {
            obj["raw_payload"] = rawDoc.object();
        } else {
            obj["raw_payload_json"] = rec.rawPayloadJson;
        }

        obj["reason"] = rec.reason;
        obj["status"] = rec.status;
        obj["resolved_target_kind"] =
            rec.resolvedTargetKind.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(rec.resolvedTargetKind);
        obj["resolved_target_id"] =
            rec.resolvedTargetId.has_value() ? QJsonValue(*rec.resolvedTargetId) : QJsonValue(QJsonValue::Null);
        obj["created_at"] = rec.createdAt;
        obj["resolved_at"] = rec.resolvedAt.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(rec.resolvedAt);
        array.append(obj);
    }

    QJsonObject root;
    root["quarantined_records"] = array;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<int> RecoveryStore::reimportAsArtifact(int recordId, const QString& artifactKind, int folderId,
                                                     QString* error) {
    if (artifactKind != "document" && artifactKind != "note" && artifactKind != "template" && artifactKind != "draft") {
        if (error) *error = "Invalid artifact kind for re-import";
        return std::nullopt;
    }

    db::Transaction tx(m_db);
    if (tx.isFailed()) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return std::nullopt;
    }

    auto recOpt = getRecord(recordId, error);
    if (!recOpt.has_value()) {
        if (error && error->isEmpty()) *error = "Quarantined record not found";
        return std::nullopt;
    }
    if (recOpt->status != "quarantined") {
        if (error) *error = "Quarantined record has already been resolved";
        return std::nullopt;
    }

    QString title =
        recOpt->title.isEmpty() ? QString("Recovered %1 #%2").arg(recOpt->sourceTable).arg(recOpt->id) : recOpt->title;
    QString content = recOpt->content.isNull() ? QString("") : recOpt->content;

    const char* insertArtSql = "INSERT INTO artifacts (kind, folder_id) VALUES (?, ?);";
    sqlite3_stmt* artStmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), insertArtSql, -1, &artStmt, nullptr) != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return std::nullopt;
    }
    const QByteArray kindUtf8 = artifactKind.toUtf8();
    sqlite3_bind_text(artStmt, 1, kindUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(artStmt, 2, folderId);
    if (sqlite3_step(artStmt) != SQLITE_DONE) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(artStmt);
        return std::nullopt;
    }
    sqlite3_finalize(artStmt);
    const int newArtId = static_cast<int>(sqlite3_last_insert_rowid(m_db.handle()));

    const char* insertVerSql =
        "INSERT INTO artifact_versions (artifact_id, title, content, metadata, is_sealed) VALUES (?, ?, ?, '', 0);";
    sqlite3_stmt* verStmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), insertVerSql, -1, &verStmt, nullptr) != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return std::nullopt;
    }
    const QByteArray titleUtf8 = title.toUtf8();
    const QByteArray contentUtf8 = content.toUtf8();
    sqlite3_bind_int(verStmt, 1, newArtId);
    sqlite3_bind_text(verStmt, 2, titleUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(verStmt, 3, contentUtf8.constData(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(verStmt) != SQLITE_DONE) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(verStmt);
        return std::nullopt;
    }
    sqlite3_finalize(verStmt);
    const int newVerId = static_cast<int>(sqlite3_last_insert_rowid(m_db.handle()));

    if (!m_db.execute(
            QString("UPDATE artifacts SET current_version_id = %1 WHERE id = %2;").arg(newVerId).arg(newArtId),
            error)) {
        return std::nullopt;
    }

    const char* updateSql =
        "UPDATE quarantined_records SET status = 'reimported', resolved_target_kind = ?, resolved_target_id = ?, "
        "resolved_at = CURRENT_TIMESTAMP WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), updateSql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, kindUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, newArtId);
    sqlite3_bind_int(stmt, 3, recordId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return std::nullopt;
    }
    sqlite3_finalize(stmt);

    if (!tx.commit()) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return std::nullopt;
    }
    return newArtId;
}

bool RecoveryStore::reassignRecord(int recordId, const QString& targetKind, int targetId, QString* error) {
    if (targetId <= 0) {
        if (error) *error = "Target ID must be positive";
        return false;
    }

    db::Transaction tx(m_db);
    if (tx.isFailed()) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return false;
    }

    auto recOpt = getRecord(recordId, error);
    if (!recOpt.has_value()) {
        if (error && error->isEmpty()) *error = "Quarantined record not found";
        return false;
    }
    if (recOpt->status != "quarantined") {
        if (error) *error = "Quarantined record has already been resolved";
        return false;
    }

    if (!targetExists(targetKind, targetId, error)) {
        return false;
    }

    const char* updateSql =
        "UPDATE quarantined_records SET status = 'reassigned', resolved_target_kind = ?, resolved_target_id = ?, "
        "resolved_at = CURRENT_TIMESTAMP WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), updateSql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return false;
    }

    const QByteArray kindUtf8 = targetKind.toUtf8();
    sqlite3_bind_text(stmt, 1, kindUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, targetId);
    sqlite3_bind_int(stmt, 3, recordId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

    if (!tx.commit()) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return false;
    }
    return true;
}

bool RecoveryStore::deleteRecordPermanently(int recordId, QString* error) {
    db::Transaction tx(m_db);
    if (tx.isFailed()) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return false;
    }

    const char* sql = "DELETE FROM quarantined_records WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return false;
    }

    sqlite3_bind_int(stmt, 1, recordId);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return false;
    }
    sqlite3_finalize(stmt);

    if (sqlite3_changes(m_db.handle()) != 1) {
        if (error) *error = "Quarantined record not found";
        return false;
    }

    if (!tx.commit()) {
        if (error) *error = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        return false;
    }
    return true;
}

bool RecoveryStore::targetExists(const QString& targetKind, int targetId, QString* error) const {
    QString sql;
    if (targetKind == "document" || targetKind == "note" || targetKind == "template" || targetKind == "draft") {
        sql = QString("SELECT COUNT(*) FROM artifacts WHERE id = %1 AND kind = '%2';").arg(targetId).arg(targetKind);
    } else if (targetKind == "artifact") {
        sql = QString("SELECT COUNT(*) FROM artifacts WHERE id = %1;").arg(targetId);
    } else if (targetKind == "message") {
        sql = QString("SELECT COUNT(*) FROM messages WHERE id = %1;").arg(targetId);
    } else if (targetKind == "folder") {
        sql = QString("SELECT COUNT(*) FROM folders WHERE id = %1;").arg(targetId);
    } else {
        if (error) *error = QString("Unsupported target kind: %1").arg(targetKind);
        return false;
    }

    int count = 0;
    if (!m_db.queryInt(sql, count, error)) {
        return false;
    }
    if (count <= 0) {
        if (error) *error = QString("Target %1 #%2 does not exist").arg(targetKind).arg(targetId);
        return false;
    }
    return true;
}

}  // namespace store
