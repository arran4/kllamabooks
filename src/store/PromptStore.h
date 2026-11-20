#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "../db/Database.h"
#include "ArtifactStore.h"

namespace store {

struct PromptRevision {
    int id = 0;
    QString sourceKind;
    QString templateId;
    QString targetKind;
    int targetId = 0;
    QString systemPrompt;
    QString promptText;
    QString model;
    bool isSealed = false;
    QString createdAt;
};

class PromptStore {
   public:
    explicit PromptStore(db::Database& db);

    Result<PromptRevision> createRevision(const QString& sourceKind, const QString& templateId,
                                          const QString& targetKind, int targetId, const QString& systemPrompt,
                                          const QString& promptText, const QString& model, bool seal = true);

    std::optional<PromptRevision> getRevision(int id) const;
    Result<bool> sealRevision(int id);
    QList<PromptRevision> getRevisionsForTarget(const QString& targetKind, int targetId) const;

   private:
    db::Database& m_db;
};

}  // namespace store
