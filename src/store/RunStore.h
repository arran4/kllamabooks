#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "../db/Database.h"
#include "ArtifactStore.h"

namespace store {

struct AiRunInput {
    int id = 0;
    int runId = 0;
    int ordinal = 0;
    int artifactId = 0;
    int versionId = 0;
    QString role = QStringLiteral("source");
};

struct AiRun {
    int id = 0;
    std::optional<int> parentRunId;
    int promptRevisionId = 0;
    QString targetKind;
    int targetId = 0;
    int baseVersionId = 0;
    QString targetAction;
    QString model;
    QString state;
    int priority = 0;
    QString workerInstanceId;
    QString leaseExpiresAt;
    QString responseText;
    int resultVersionId = 0;
    QString lastError;
    QString createdAt;
    QString startedAt;
    QString finishedAt;
};

class RunStore {
   public:
    explicit RunStore(db::Database& db);

    Result<AiRun> enqueueRun(int promptRevisionId, const QString& targetKind, int targetId, int baseVersionId,
                             const QString& targetAction, const QString& model, int priority = 0,
                             std::optional<int> parentRunId = std::nullopt, const QList<AiRunInput>& inputs = {});

    Result<AiRun> createDescendantRun(int parentRunId, const QString& overridePromptText = QString(),
                                      const QString& overrideModel = QString());

    std::optional<AiRun> getRun(int id) const;
    QList<AiRun> listRuns(const QString& stateFilter = QString()) const;
    QList<AiRunInput> getRunInputs(int runId) const;
    QList<AiRunInput> resolveInputsForRerun(int runId, bool exactSnapshot) const;

    Result<bool> claimRun(int runId, const QString& workerInstanceId, int leaseSeconds = 300);
    Result<bool> renewLease(int runId, const QString& workerInstanceId, int leaseSeconds = 300);
    Result<bool> completeRun(int runId, const QString& responseText, int resultVersionId = 0);
    Result<bool> failRun(int runId, const QString& errorMessage);
    Result<bool> cancelRun(int runId);
    Result<bool> archiveRun(int runId);
    Result<bool> updatePriority(int runId, int priority);

    Result<int> recoverStaleRuns(const QString& currentWorkerInstanceId);
    Result<ArtifactVersion> applyRunOutput(int runId, const QString& title, const QString& metadata = QString());

   private:
    db::Database& m_db;
};

}  // namespace store
