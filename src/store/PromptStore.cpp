#include "PromptStore.h"

#include <sqlcipher/sqlite3.h>

namespace store {

namespace {

QString colText(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return QString();
    const unsigned char* txt = sqlite3_column_text(stmt, idx);
    return txt ? QString::fromUtf8(reinterpret_cast<const char*>(txt)) : QString();
}

PromptRevision readRevisionRow(sqlite3_stmt* stmt) {
    PromptRevision rev;
    rev.id = sqlite3_column_int(stmt, 0);
    rev.sourceKind = colText(stmt, 1);
    rev.templateId = colText(stmt, 2);
    rev.targetKind = colText(stmt, 3);
    rev.targetId = sqlite3_column_int(stmt, 4);
    rev.systemPrompt = colText(stmt, 5);
    rev.promptText = colText(stmt, 6);
    rev.model = colText(stmt, 7);
    rev.isSealed = sqlite3_column_int(stmt, 8) != 0;
    rev.createdAt = colText(stmt, 9);
    return rev;
}

}  // namespace

// ============================================================================
// Constructor
// ============================================================================

PromptStore::PromptStore(db::Database& db) : m_db(db) {}

// ============================================================================
// Public Methods
// ============================================================================

Result<PromptRevision> PromptStore::createRevision(const QString& sourceKind, const QString& templateId,
                                                   const QString& targetKind, int targetId, const QString& systemPrompt,
                                                   const QString& promptText, const QString& model, bool seal) {
    if (!m_db.handle()) {
        return Result<PromptRevision>::fail(TransitionError::DatabaseError, "Database not open");
    }

    const char* sql =
        "INSERT INTO prompt_revisions "
        "(source_kind, template_id, target_kind, target_id, system_prompt, prompt_text, model, is_sealed) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) RETURNING id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<PromptRevision>::fail(TransitionError::DatabaseError,
                                            QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }

    const QByteArray srcUtf8 = sourceKind.toUtf8();
    const QByteArray tplUtf8 = templateId.toUtf8();
    const QByteArray tgtKindUtf8 = targetKind.toUtf8();
    const QByteArray sysUtf8 = systemPrompt.toUtf8();
    const QByteArray promptUtf8 = promptText.toUtf8();
    const QByteArray modelUtf8 = model.toUtf8();

    sqlite3_bind_text(stmt, 1, srcUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, tplUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, tgtKindUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, targetId);
    sqlite3_bind_text(stmt, 5, sysUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, promptUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, modelUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, seal ? 1 : 0);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<PromptRevision>::fail(TransitionError::DatabaseError, err);
    }

    const int newId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    auto revOpt = getRevision(newId);
    if (!revOpt.has_value()) {
        return Result<PromptRevision>::fail(TransitionError::DatabaseError, "Failed to read inserted prompt revision");
    }
    return Result<PromptRevision>::success(*revOpt);
}

std::optional<PromptRevision> PromptStore::getRevision(int id) const {
    if (!m_db.handle() || id <= 0) return std::nullopt;

    const char* sql =
        "SELECT id, source_kind, template_id, target_kind, target_id, system_prompt, prompt_text, model, "
        "is_sealed, created_at FROM prompt_revisions WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);
    std::optional<PromptRevision> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out = readRevisionRow(stmt);
    }
    sqlite3_finalize(stmt);
    return out;
}

Result<bool> PromptStore::sealRevision(int id) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (id <= 0) return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid revision ID");

    auto existing = getRevision(id);
    if (!existing.has_value()) return Result<bool>::fail(TransitionError::NotFound, "Prompt revision not found");
    if (existing->isSealed) return Result<bool>::success(true);

    const char* sql = "UPDATE prompt_revisions SET is_sealed = 1 WHERE id = ?;";
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

QList<PromptRevision> PromptStore::getRevisionsForTarget(const QString& targetKind, int targetId) const {
    QList<PromptRevision> list;
    if (!m_db.handle()) return list;

    const char* sql =
        "SELECT id, source_kind, template_id, target_kind, target_id, system_prompt, prompt_text, model, "
        "is_sealed, created_at FROM prompt_revisions WHERE target_kind = ? AND target_id = ? ORDER BY id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return list;

    const QByteArray kindUtf8 = targetKind.toUtf8();
    sqlite3_bind_text(stmt, 1, kindUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, targetId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        list.append(readRevisionRow(stmt));
    }
    sqlite3_finalize(stmt);
    return list;
}

}  // namespace store
