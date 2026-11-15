#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

#include "../db/Database.h"
#include "ArtifactStore.h"
#include "ItemRef.h"

namespace store {

struct FolderRecord {
    int id = 0;
    int parentId = 0;
    QString name;
    QString rootType;
    QDateTime timestamp;
    int position = 0;
    bool isExpanded = false;
};

class FolderStore {
   public:
    explicit FolderStore(db::Database& db);

    Result<int> createFolder(int parentId, const QString& name, const QString& rootType, bool isExpanded = false);
    std::optional<FolderRecord> getFolder(int id) const;
    Result<QList<FolderRecord>> getFolders(const QString& rootType = QString()) const;
    Result<bool> renameFolder(int id, const QString& newName);
    Result<bool> setExpanded(int id, bool expanded);
    Result<bool> moveFolder(int folderId, int newParentId);
    Result<bool> moveItem(const ItemRef& item, int targetFolderId);
    Result<bool> deleteFolder(int id);
    bool wouldCreateCycle(int folderId, int candidateParentId) const;

   private:
    std::optional<int> resolveArtifactId(ArtifactKind kind, int idOrLegacyId) const;

    db::Database& m_db;
};

}  // namespace store
