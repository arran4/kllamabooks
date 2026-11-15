#ifndef STORE_RECOVERYSTORE_H
#define STORE_RECOVERYSTORE_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <optional>

namespace db {
class Database;
}

namespace store {

struct QuarantinedRecord {
    int id = 0;
    QString sourceTable;
    std::optional<int> legacyId;
    QString targetType;
    std::optional<int> parentReference;
    QString title;
    QString content;
    QString rawPayloadJson;
    QString reason;
    QString status;
    QString resolvedTargetKind;
    std::optional<int> resolvedTargetId;
    QString createdAt;
    QString resolvedAt;
};

class RecoveryStore {
   public:
    explicit RecoveryStore(db::Database& db);
    ~RecoveryStore() = default;

    std::optional<int> addQuarantinedRecord(const QString& sourceTable, std::optional<int> legacyId,
                                            const QString& targetType, std::optional<int> parentReference,
                                            const QString& title, const QString& content, const QString& rawPayloadJson,
                                            const QString& reason, QString* error = nullptr);

    std::optional<QuarantinedRecord> getRecord(int id, QString* error = nullptr) const;
    QVector<QuarantinedRecord> getRecords(const QString& statusFilter = QString(),
                                          const QString& sourceTableFilter = QString(), QString* error = nullptr) const;
    int getQuarantinedCount(QString* error = nullptr) const;

    QByteArray exportRecordsToJson(const QVector<int>& ids = {}, QString* error = nullptr) const;

    std::optional<int> reimportAsArtifact(int recordId, const QString& artifactKind, int folderId = 0,
                                          QString* error = nullptr);
    bool reassignRecord(int recordId, const QString& targetKind, int targetId, QString* error = nullptr);
    bool deleteRecordPermanently(int recordId, QString* error = nullptr);

   private:
    db::Database& m_db;

    bool targetExists(const QString& targetKind, int targetId, QString* error = nullptr) const;
};

}  // namespace store

#endif  // STORE_RECOVERYSTORE_H
