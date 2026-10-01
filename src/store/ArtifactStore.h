#ifndef STORE_ARTIFACTSTORE_H
#define STORE_ARTIFACTSTORE_H

#include <QList>
#include <QString>
#include <optional>
#include <variant>

namespace db {
class Database;
}

namespace store {

enum class ArtifactKind { Document, Note, Template, Draft };

struct Artifact {
    int id = 0;
    ArtifactKind kind = ArtifactKind::Document;
    int folderId = 0;
    int currentVersionId = 0;
    QString createdAt;
};

struct ArtifactVersion {
    int id = 0;
    int artifactId = 0;
    std::optional<int> parentId = std::nullopt;
    std::optional<int> forkedFromVersionId = std::nullopt;
    QString title;
    QString content;
    QString metadata;
    bool isSealed = false;
    QString createdAt;
};

enum class TransitionError { Conflict, Sealed, InvalidCrossReference, NotFound, DatabaseError, UnsupportedOperation };

template <typename T>
struct Result {
    std::optional<T> value;
    std::optional<TransitionError> error;
    QString errorMessage;

    static Result<T> success(const T& val) { return {val, std::nullopt, ""}; }
    static Result<T> fail(TransitionError err, const QString& msg = "") { return {std::nullopt, err, msg}; }

    bool isSuccess() const { return value.has_value(); }
};

class ArtifactStore {
   public:
    explicit ArtifactStore(db::Database& db);

    Result<ArtifactVersion> createArtifact(ArtifactKind kind, int folderId, const QString& title,
                                           const QString& content, const QString& metadata);
    Result<ArtifactVersion> editVersion(int expectedVersionId, const QString& title, const QString& content,
                                        const QString& metadata);
    Result<ArtifactVersion> sealVersion(int versionId);
    Result<ArtifactVersion> createMutableDescendant(int expectedBaseVersionId, const QString& title,
                                                    const QString& content, const QString& metadata);
    Result<ArtifactVersion> restoreVersion(int expectedBaseVersionId, int versionToRestoreId);
    Result<ArtifactVersion> forkArtifact(int expectedBaseVersionId, int folderId);

    std::optional<Artifact> getArtifact(int id) const;
    std::optional<ArtifactVersion> getVersion(int id) const;
    std::optional<ArtifactVersion> getCurrentVersion(int artifactId) const;
    Result<QList<Artifact>> getArtifacts(std::optional<ArtifactKind> kind = std::nullopt,
                                         std::optional<int> folderId = std::nullopt) const;
    Result<Artifact> resolveSourceArtifact(int draftArtifactId) const;

    Result<bool> deleteArtifact(int id);
    Result<Artifact> moveArtifact(int id, int newFolderId);
    Result<ArtifactVersion> createDraftFromVersion(int expectedBaseVersionId, int folderId);

   private:
    db::Database& m_db;

    Result<Artifact> getArtifactResult(int id) const;
    Result<ArtifactVersion> getVersionResult(int id) const;
};

}  // namespace store

#endif  // STORE_ARTIFACTSTORE_H
