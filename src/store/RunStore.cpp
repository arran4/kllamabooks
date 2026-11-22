#include "RunStore.h"

#include <sqlcipher/sqlite3.h>

#include "../db/Transaction.h"
#include "PromptStore.h"

namespace store {

namespace {

QString colText(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return QString();
    const unsigned char* txt = sqlite3_column_text(stmt, idx);
    return txt ? QString::fromUtf8(reinterpret_cast<const char*>(txt)) : QString();
}

std::optional<int> colOptInt(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int(stmt, idx);
}

AiRun readRunRow(sqlite3_stmt* stmt) {
    AiRun run;
    run.id = sqlite3_column_int(stmt, 0);
    run.parentRunId = colOptInt(stmt, 1);
    run.promptRevisionId = sqlite3_column_int(stmt, 2);
    run.targetKind = colText(stmt, 3);
    run.targetId = sqlite3_column_int(stmt, 4);
    run.baseVersionId = sqlite3_column_int(stmt, 5);
    run.targetAction = colText(stmt, 6);
    run.model = colText(stmt, 7);
    run.state = colText(stmt, 8);
    run.priority = sqlite3_column_int(stmt, 9);
    run.workerInstanceId = colText(stmt, 10);
    run.leaseExpiresAt = colText(stmt, 11);
    run.responseText = colText(stmt, 12);
    run.resultVersionId = sqlite3_column_int(stmt, 13);
    run.lastError = colText(stmt, 14);
    run.createdAt = colText(stmt, 15);
    run.startedAt = colText(stmt, 16);
    run.finishedAt = colText(stmt, 17);
    return run;
}

}  // namespace

// ============================================================================
// Constructor
// ============================================================================

RunStore::RunStore(db::Database& db) : m_db(db) {}

// ============================================================================
// Public Methods
// ============================================================================

Result<AiRun> RunStore::enqueueRun(int promptRevisionId, const QString& targetKind, int targetId, int baseVersionId,
                                   const QString& targetAction, const QString& model, int priority,
                                   std::optional<int> parentRunId, const QList<AiRunInput>& inputs) {
    if (!m_db.handle()) return Result<AiRun>::fail(TransitionError::DatabaseError, "Database not open");
    if (promptRevisionId <= 0) {
        return Result<AiRun>::fail(TransitionError::InvalidCrossReference, "promptRevisionId must be positive");
    }

    PromptStore promptStore(m_db);
    if (!promptStore.getRevision(promptRevisionId).has_value()) {
        return Result<AiRun>::fail(TransitionError::NotFound, "Prompt revision not found");
    }

    if (!m_db.execute("BEGIN IMMEDIATE;")) {
        return Result<AiRun>::fail(TransitionError::DatabaseError, "Failed to start transaction");
    }
    auto rollback = [this](TransitionError err, const QString& msg) {
        m_db.execute("ROLLBACK;");
        return Result<AiRun>::fail(err, msg);
    };

    const char* sql =
        "INSERT INTO ai_runs "
        "(parent_run_id, prompt_revision_id, target_kind, target_id, base_version_id, target_action, model, state, "
        "priority) VALUES (?, ?, ?, ?, ?, ?, ?, 'pending', ?) RETURNING id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return rollback(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }

    if (parentRunId.has_value()) {
        sqlite3_bind_int(stmt, 1, *parentRunId);
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    sqlite3_bind_int(stmt, 2, promptRevisionId);
    const QByteArray kindUtf8 = targetKind.toUtf8();
    const QByteArray actUtf8 = targetAction.toUtf8();
    const QByteArray modelUtf8 = model.toUtf8();
    sqlite3_bind_text(stmt, 3, kindUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, targetId);
    sqlite3_bind_int(stmt, 5, baseVersionId);
    sqlite3_bind_text(stmt, 6, actUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, modelUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, priority);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return rollback(TransitionError::DatabaseError, err);
    }
    const int runId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    for (int i = 0; i < inputs.size(); ++i) {
        const AiRunInput& inp = inputs.at(i);
        const int ordinal = inp.ordinal >= 0 ? inp.ordinal : i;
        const char* inSql =
            "INSERT INTO ai_run_inputs (run_id, ordinal, artifact_id, version_id, role) VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* inStmt = nullptr;
        if (sqlite3_prepare_v2(m_db.handle(), inSql, -1, &inStmt, nullptr) != SQLITE_OK) {
            return rollback(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
        }
        const QByteArray roleUtf8 = (inp.role.isEmpty() ? QStringLiteral("source") : inp.role).toUtf8();
        sqlite3_bind_int(inStmt, 1, runId);
        sqlite3_bind_int(inStmt, 2, ordinal);
        sqlite3_bind_int(inStmt, 3, inp.artifactId);
        sqlite3_bind_int(inStmt, 4, inp.versionId);
        sqlite3_bind_text(inStmt, 5, roleUtf8.constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(inStmt) != SQLITE_DONE) {
            const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
            sqlite3_finalize(inStmt);
            return rollback(TransitionError::Conflict, err);
        }
        sqlite3_finalize(inStmt);
    }

    if (!m_db.execute("COMMIT;")) {
        return rollback(TransitionError::DatabaseError, "Failed to commit enqueueRun");
    }

    auto runOpt = getRun(runId);
    if (!runOpt.has_value()) {
        return Result<AiRun>::fail(TransitionError::DatabaseError, "Failed to reload enqueued run");
    }
    return Result<AiRun>::success(*runOpt);
}

Result<AiRun> RunStore::createDescendantRun(int parentRunId, const QString& overridePromptText,
                                            const QString& overrideModel) {
    if (!m_db.handle()) return Result<AiRun>::fail(TransitionError::DatabaseError, "Database not open");
    auto parentOpt = getRun(parentRunId);
    if (!parentOpt.has_value()) return Result<AiRun>::fail(TransitionError::NotFound, "Parent run not found");

    const AiRun& parent = *parentOpt;
    const QString modelToUse = overrideModel.isEmpty() ? parent.model : overrideModel;
    int revIdToUse = parent.promptRevisionId;

    if (!overridePromptText.isEmpty()) {
        PromptStore promptStore(m_db);
        auto oldRev = promptStore.getRevision(parent.promptRevisionId);
        const QString sysPrompt = oldRev.has_value() ? oldRev->systemPrompt : QString();
        const QString tplId = oldRev.has_value() ? oldRev->templateId : QString();
        auto revRes = promptStore.createRevision("user", tplId, parent.targetKind, parent.targetId, sysPrompt,
                                                 overridePromptText, modelToUse, true);
        if (!revRes.isSuccess()) {
            return Result<AiRun>::fail(revRes.error.value_or(TransitionError::DatabaseError), revRes.errorMessage);
        }
        revIdToUse = revRes.value->id;
    }

    const QList<AiRunInput> parentInputs = getRunInputs(parentRunId);
    return enqueueRun(revIdToUse, parent.targetKind, parent.targetId, parent.baseVersionId, parent.targetAction,
                      modelToUse, parent.priority, parentRunId, parentInputs);
}

std::optional<AiRun> RunStore::getRun(int id) const {
    if (!m_db.handle() || id <= 0) return std::nullopt;

    const char* sql =
        "SELECT id, parent_run_id, prompt_revision_id, target_kind, target_id, base_version_id, target_action, "
        "model, state, priority, worker_instance_id, lease_expires_at, response_text, result_version_id, last_error, "
        "created_at, started_at, finished_at FROM ai_runs WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);
    std::optional<AiRun> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out = readRunRow(stmt);
    }
    sqlite3_finalize(stmt);
    return out;
}

QList<AiRun> RunStore::listRuns(const QString& stateFilter) const {
    QList<AiRun> list;
    if (!m_db.handle()) return list;

    const bool hasFilter = !stateFilter.trimmed().isEmpty();
    const char* sql = hasFilter ? "SELECT id, parent_run_id, prompt_revision_id, target_kind, target_id, "
                                  "base_version_id, target_action, model, state, priority, worker_instance_id, "
                                  "lease_expires_at, response_text, result_version_id, last_error, created_at, "
                                  "started_at, finished_at FROM ai_runs WHERE state = ? ORDER BY priority DESC, id ASC;"
                                : "SELECT id, parent_run_id, prompt_revision_id, target_kind, target_id, "
                                  "base_version_id, target_action, model, state, priority, worker_instance_id, "
                                  "lease_expires_at, response_text, result_version_id, last_error, created_at, "
                                  "started_at, finished_at FROM ai_runs ORDER BY priority DESC, id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return list;

    const QByteArray filterUtf8 = stateFilter.toUtf8();
    if (hasFilter) {
        sqlite3_bind_text(stmt, 1, filterUtf8.constData(), -1, SQLITE_TRANSIENT);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        list.append(readRunRow(stmt));
    }
    sqlite3_finalize(stmt);
    return list;
}

QList<AiRunInput> RunStore::getRunInputs(int runId) const {
    QList<AiRunInput> inputs;
    if (!m_db.handle() || runId <= 0) return inputs;

    const char* sql =
        "SELECT id, run_id, ordinal, artifact_id, version_id, role FROM ai_run_inputs "
        "WHERE run_id = ? ORDER BY ordinal ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return inputs;
    sqlite3_bind_int(stmt, 1, runId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AiRunInput inp;
        inp.id = sqlite3_column_int(stmt, 0);
        inp.runId = sqlite3_column_int(stmt, 1);
        inp.ordinal = sqlite3_column_int(stmt, 2);
        inp.artifactId = sqlite3_column_int(stmt, 3);
        inp.versionId = sqlite3_column_int(stmt, 4);
        inp.role = colText(stmt, 5);
        inputs.append(inp);
    }
    sqlite3_finalize(stmt);
    return inputs;
}

QList<AiRunInput> RunStore::resolveInputsForRerun(int runId, bool exactSnapshot) const {
    QList<AiRunInput> inputs = getRunInputs(runId);
    if (exactSnapshot || !m_db.handle()) return inputs;

    ArtifactStore artStore(m_db);
    for (AiRunInput& inp : inputs) {
        if (inp.artifactId > 0) {
            if (auto artOpt = artStore.getArtifact(inp.artifactId)) {
                inp.versionId = artOpt->currentVersionId;
            }
        }
    }
    return inputs;
}

Result<bool> RunStore::claimRun(int runId, const QString& workerInstanceId, int leaseSeconds) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (runId <= 0 || workerInstanceId.trimmed().isEmpty() || leaseSeconds <= 0) {
        return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid claimRun arguments");
    }

    const QString sql = QString(
                            "UPDATE ai_runs SET state = 'running', worker_instance_id = ?, "
                            "lease_expires_at = datetime('now', '+%1 seconds'), started_at = CURRENT_TIMESTAMP "
                            "WHERE id = ? AND state = 'pending';")
                            .arg(leaseSeconds);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    const QByteArray workerUtf8 = workerInstanceId.toUtf8();
    sqlite3_bind_text(stmt, 1, workerUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, runId);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    const int changes = sqlite3_changes(m_db.handle());
    sqlite3_finalize(stmt);
    if (changes == 0) {
        return Result<bool>::fail(TransitionError::Conflict, "Run already claimed or not pending");
    }
    return Result<bool>::success(true);
}

Result<bool> RunStore::renewLease(int runId, const QString& workerInstanceId, int leaseSeconds) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (runId <= 0 || workerInstanceId.trimmed().isEmpty() || leaseSeconds <= 0) {
        return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid renewLease arguments");
    }

    const QString sql = QString(
                            "UPDATE ai_runs SET lease_expires_at = datetime('now', '+%1 seconds') "
                            "WHERE id = ? AND state = 'running' AND worker_instance_id = ?;")
                            .arg(leaseSeconds);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    const QByteArray workerUtf8 = workerInstanceId.toUtf8();
    sqlite3_bind_int(stmt, 1, runId);
    sqlite3_bind_text(stmt, 2, workerUtf8.constData(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    const int changes = sqlite3_changes(m_db.handle());
    sqlite3_finalize(stmt);
    if (changes == 0) return Result<bool>::fail(TransitionError::Conflict, "Lease renewal rejected");
    return Result<bool>::success(true);
}

Result<bool> RunStore::completeRun(int runId, const QString& responseText, int resultVersionId) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (runId <= 0) return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid runId");

    const char* sql =
        "UPDATE ai_runs SET state = 'completed', response_text = ?, result_version_id = ?, "
        "last_error = '', finished_at = CURRENT_TIMESTAMP WHERE id = ? AND state IN ('running', 'pending');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    const QByteArray respUtf8 = responseText.toUtf8();
    sqlite3_bind_text(stmt, 1, respUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, resultVersionId);
    sqlite3_bind_int(stmt, 3, runId);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    const int changes = sqlite3_changes(m_db.handle());
    sqlite3_finalize(stmt);
    if (changes == 0) return Result<bool>::fail(TransitionError::Conflict, "Run not in completable state");
    return Result<bool>::success(true);
}

Result<bool> RunStore::failRun(int runId, const QString& errorMessage) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (runId <= 0) return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid runId");

    const char* sql =
        "UPDATE ai_runs SET state = 'failed', last_error = ?, finished_at = CURRENT_TIMESTAMP "
        "WHERE id = ? AND state IN ('running', 'pending');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    const QByteArray errUtf8 = errorMessage.toUtf8();
    sqlite3_bind_text(stmt, 1, errUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, runId);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    sqlite3_finalize(stmt);
    return Result<bool>::success(true);
}

Result<bool> RunStore::cancelRun(int runId) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (runId <= 0) return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid runId");

    const char* sql =
        "UPDATE ai_runs SET state = 'cancelled', finished_at = CURRENT_TIMESTAMP "
        "WHERE id = ? AND state IN ('pending', 'running');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    sqlite3_bind_int(stmt, 1, runId);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    sqlite3_finalize(stmt);
    return Result<bool>::success(true);
}

Result<bool> RunStore::archiveRun(int runId) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (runId <= 0) return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid runId");

    const char* sql =
        "UPDATE ai_runs SET state = 'archived', finished_at = COALESCE(finished_at, CURRENT_TIMESTAMP) "
        "WHERE id = ? AND state IN ('completed', 'applied', 'failed', 'conflict', 'cancelled', 'archived');";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    sqlite3_bind_int(stmt, 1, runId);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    const int changes = sqlite3_changes(m_db.handle());
    sqlite3_finalize(stmt);
    if (changes == 0) {
        return Result<bool>::fail(TransitionError::Conflict, "Cannot archive active or missing run");
    }
    return Result<bool>::success(true);
}

Result<bool> RunStore::updatePriority(int runId, int priority) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (runId <= 0) return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid runId");

    const char* sql = "UPDATE ai_runs SET priority = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    sqlite3_bind_int(stmt, 1, priority);
    sqlite3_bind_int(stmt, 2, runId);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    sqlite3_finalize(stmt);
    return Result<bool>::success(true);
}

Result<int> RunStore::recoverStaleRuns(const QString& currentWorkerInstanceId) {
    if (!m_db.handle()) return Result<int>::fail(TransitionError::DatabaseError, "Database not open");
    if (currentWorkerInstanceId.trimmed().isEmpty()) {
        return Result<int>::fail(TransitionError::InvalidCrossReference, "currentWorkerInstanceId required");
    }

    const char* sql =
        "UPDATE ai_runs SET state = 'pending', worker_instance_id = '', lease_expires_at = NULL, "
        "last_error = 'Recovered stale run after restart/lease expiry' "
        "WHERE state = 'running' AND (worker_instance_id != ? OR lease_expires_at <= datetime('now'));";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<int>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    const QByteArray workerUtf8 = currentWorkerInstanceId.toUtf8();
    sqlite3_bind_text(stmt, 1, workerUtf8.constData(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<int>::fail(TransitionError::DatabaseError, err);
    }
    const int recovered = sqlite3_changes(m_db.handle());
    sqlite3_finalize(stmt);
    return Result<int>::success(recovered);
}

Result<ArtifactVersion> RunStore::applyRunOutput(int runId, const QString& title, const QString& metadata) {
    if (!m_db.handle()) return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Database not open");
    auto runOpt = getRun(runId);
    if (!runOpt.has_value()) return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Run not found");
    const AiRun& run = *runOpt;
    if (run.state != QLatin1String("completed")) {
        return Result<ArtifactVersion>::fail(TransitionError::Conflict, "Run must be completed before applying");
    }
    if (run.targetId <= 0 || run.baseVersionId <= 0) {
        return Result<ArtifactVersion>::fail(TransitionError::InvalidCrossReference,
                                             "Run missing target artifact or baseVersionId");
    }

    ArtifactStore artStore(m_db);
    auto artOpt = artStore.getArtifact(run.targetId);
    if (!artOpt.has_value()) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Target artifact not found");
    }
    if (artOpt->currentVersionId != run.baseVersionId) {
        m_db.execute(QString("UPDATE ai_runs SET state = 'conflict', "
                             "last_error = 'Target artifact was modified after run started' WHERE id = %1;")
                         .arg(runId));
        return Result<ArtifactVersion>::fail(
            TransitionError::Conflict,
            "Target artifact has diverged from baseVersionId; manual review required before applying");
    }

    auto baseVerOpt = artStore.getVersion(run.baseVersionId);
    if (!baseVerOpt.has_value()) {
        return Result<ArtifactVersion>::fail(TransitionError::NotFound, "Base version not found");
    }

    db::Transaction tx(m_db);
    if (tx.isFailed()) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError,
                                             "Failed to start applyRunOutput transaction");
    }

    Result<ArtifactVersion> writeRes =
        baseVerOpt->isSealed ? artStore.createMutableDescendant(run.baseVersionId, title, run.responseText, metadata)
                             : artStore.editVersion(run.baseVersionId, title, run.responseText, metadata);
    if (!writeRes.isSuccess()) {
        tx.rollback();
        return writeRes;
    }

    const char* updateSql =
        "UPDATE ai_runs SET state = 'applied', result_version_id = ?, "
        "finished_at = COALESCE(finished_at, CURRENT_TIMESTAMP) "
        "WHERE id = ? AND state = 'completed';";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), updateSql, -1, &stmt, nullptr) != SQLITE_OK) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        tx.rollback();
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, err);
    }
    sqlite3_bind_int(stmt, 1, writeRes.value->id);
    sqlite3_bind_int(stmt, 2, runId);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        tx.rollback();
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, err);
    }
    const int changes = sqlite3_changes(m_db.handle());
    sqlite3_finalize(stmt);
    if (changes != 1) {
        tx.rollback();
        return Result<ArtifactVersion>::fail(TransitionError::Conflict,
                                             "Run state changed concurrently before apply committed");
    }

    if (!tx.commit()) {
        return Result<ArtifactVersion>::fail(TransitionError::DatabaseError, "Failed to commit applyRunOutput");
    }
    return writeRes;
}

}  // namespace store
