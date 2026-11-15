#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "../db/Database.h"
#include "ArtifactStore.h"
#include "ItemRef.h"

namespace store {

struct SearchHit {
    ItemRef item;
    QString title;
    QString snippet;
    int folderId = 0;
    QString updatedAt;
};

class SearchStore {
   public:
    explicit SearchStore(db::Database& db);

    Result<bool> upsertItem(const ItemRef& item, int folderId, const QString& title, const QString& body);
    Result<bool> removeItem(const ItemRef& item);
    Result<int> rebuildIndex();

    QList<SearchHit> search(const QString& query, std::optional<ItemKind> kindFilter = std::nullopt,
                            int limit = 50) const;

   private:
    db::Database& m_db;
};

}  // namespace store
