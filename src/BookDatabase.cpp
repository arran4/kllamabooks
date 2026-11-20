#include "BookDatabase.h"

#include <sqlcipher/sqlite3.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <cerrno>

#include "db/Database.h"
#include "db/MigrationFactory.h"
#include "store/ArtifactStore.h"
#include "store/FolderStore.h"

#ifdef Q_OS_UNIX
#include <csignal>
#endif

namespace {
constexpr int CURRENT_SCHEMA_VERSION = 28;

QString escapeSqlLiteral(const QString& input) {
    QString escaped = input;
    escaped.replace("'", "''");
    return escaped;
}

bool copyFileOwnerOnly(const QString& sourcePath, const QString& destPath, QString* error) {
    QFile src(sourcePath);
    if (!src.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("Failed to open source file for copy: %1").arg(src.errorString());
        return false;
    }

    QFile::remove(destPath);
    QFile dst(destPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QString("Failed to create destination file: %1").arg(dst.errorString());
        return false;
    }
    // Restrict permissions before writing payload bytes so no world/group-readable window exists.
    if (!dst.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        dst.close();
        QFile::remove(destPath);
        if (error) *error = "Failed to restrict destination file permissions";
        return false;
    }

    constexpr qint64 kChunkSize = 65536;
    QByteArray buffer(kChunkSize, Qt::Uninitialized);
    while (!src.atEnd()) {
        const qint64 bytesRead = src.read(buffer.data(), kChunkSize);
        if (bytesRead < 0) {
            dst.close();
            QFile::remove(destPath);
            if (error) *error = QString("Failed to read source file during copy: %1").arg(src.errorString());
            return false;
        }
        qint64 totalWritten = 0;
        while (totalWritten < bytesRead) {
            const qint64 bytesWritten = dst.write(buffer.constData() + totalWritten, bytesRead - totalWritten);
            if (bytesWritten <= 0) {
                dst.close();
                QFile::remove(destPath);
                if (error) *error = QString("Failed to write destination file during copy: %1").arg(dst.errorString());
                return false;
            }
            totalWritten += bytesWritten;
        }
    }

    if (!dst.flush()) {
        dst.close();
        QFile::remove(destPath);
        if (error) *error = QString("Failed to flush destination file during copy: %1").arg(dst.errorString());
        return false;
    }
    dst.close();
    return true;
}

bool verifyEncryptedFileIntegrity(const QString& path, const QString& password, QString* error) {
    QFile file(path);
    if (!file.exists()) {
        if (error) *error = "Database file does not exist";
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = "Failed to open database file for verification";
        return false;
    }
    const QByteArray header = file.read(16);
    file.close();
    if (header.isEmpty()) {
        if (error) *error = "Database file is empty";
        return false;
    }
    if (!password.isEmpty() && header.startsWith("SQLite format 3")) {
        if (error) *error = "Refusing unencrypted plaintext SQLite file";
        return false;
    }

    sqlite3* checkDb = nullptr;
    if (sqlite3_open(path.toUtf8().constData(), &checkDb) != SQLITE_OK) {
        if (error) *error = "sqlite3_open failed during verification";
        if (checkDb) sqlite3_close(checkDb);
        return false;
    }

    if (!password.isEmpty()) {
        const QByteArray keySql = QString("PRAGMA key = '%1';").arg(escapeSqlLiteral(password)).toUtf8();
        if (sqlite3_exec(checkDb, keySql.constData(), nullptr, nullptr, nullptr) != SQLITE_OK) {
            if (error) *error = "Failed to set SQLCipher key during verification";
            sqlite3_close(checkDb);
            return false;
        }
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(checkDb, "PRAGMA integrity_check;", -1, &stmt, nullptr) != SQLITE_OK) {
        if (error) *error = "Invalid password or unreadable SQLCipher database";
        sqlite3_close(checkDb);
        return false;
    }
    QString integrityResult;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) integrityResult = QString::fromUtf8(reinterpret_cast<const char*>(text));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(checkDb);

    if (integrityResult != "ok") {
        if (error) *error = QString("Database integrity check failed: %1").arg(integrityResult);
        return false;
    }
    return true;
}

QString artifactKindString(store::ArtifactKind kind) {
    switch (kind) {
        case store::ArtifactKind::Document:
            return "document";
        case store::ArtifactKind::Note:
            return "note";
        case store::ArtifactKind::Template:
            return "template";
        case store::ArtifactKind::Draft:
            return "draft";
    }
    return "document";
}

std::optional<store::ArtifactKind> parseArtifactKind(const QString& targetType) {
    if (targetType == "document" || targetType == "documents") return store::ArtifactKind::Document;
    if (targetType == "note" || targetType == "notes") return store::ArtifactKind::Note;
    if (targetType == "template" || targetType == "templates") return store::ArtifactKind::Template;
    if (targetType == "draft" || targetType == "drafts") return store::ArtifactKind::Draft;
    return std::nullopt;
}

std::optional<int> resolveArtifactIdForKind(sqlite3* db, store::ArtifactKind kind, int id) {
    if (!db || id <= 0) return std::nullopt;

    const QString kindStr = artifactKindString(kind);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT id FROM artifacts WHERE id = ? AND kind = ?;", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        const QByteArray kUtf8 = kindStr.toUtf8();
        sqlite3_bind_text(stmt, 2, kUtf8.constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int foundId = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            return foundId;
        }
        sqlite3_finalize(stmt);
    }

    const char* mapSql = nullptr;
    switch (kind) {
        case store::ArtifactKind::Document:
            mapSql = "SELECT artifact_id FROM legacy_document_mapping WHERE document_id = ?;";
            break;
        case store::ArtifactKind::Note:
            mapSql = "SELECT artifact_id FROM legacy_note_mapping WHERE note_id = ?;";
            break;
        case store::ArtifactKind::Template:
            mapSql = "SELECT artifact_id FROM legacy_template_mapping WHERE template_id = ?;";
            break;
        case store::ArtifactKind::Draft:
            mapSql = "SELECT artifact_id FROM legacy_draft_mapping WHERE draft_id = ?;";
            break;
    }
    if (mapSql && sqlite3_prepare_v2(db, mapSql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int artId = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            return artId;
        }
        sqlite3_finalize(stmt);
    }
    return std::nullopt;
}

bool updateOrAdvanceArtifact(db::Database& database, int artifactId, const QString& newTitle, const QString& newContent,
                             const QString& metadata) {
    store::ArtifactStore store(database);
    auto curVer = store.getCurrentVersion(artifactId);
    if (!curVer.has_value()) return false;

    if (!curVer->isSealed) {
        return store.editVersion(curVer->id, newTitle, newContent, metadata).isSuccess();
    }
    return store.createMutableDescendant(curVer->id, newTitle, newContent, metadata).isSuccess();
}

int findParentDocumentArtifactId(sqlite3* db, int artifactId) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT src.artifact_id FROM artifact_versions v "
                           "JOIN artifact_versions src ON src.id = v.forked_from_version_id "
                           "WHERE v.artifact_id = ? AND v.forked_from_version_id IS NOT NULL "
                           "ORDER BY v.id ASC LIMIT 1;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_int(stmt, 1, artifactId);
    int parentArtId = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        parentArtId = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return parentArtId;
}
}  // namespace

BookDatabase::BookDatabase(const QString& filepath) : m_filepath(filepath), m_db(nullptr), m_isOpen(false) {}

BookDatabase::~BookDatabase() { close(); }

bool BookDatabase::open(const QString& password) {
    if (m_isOpen) return true;

    int rc = sqlite3_open(m_filepath.toUtf8().constData(), reinterpret_cast<sqlite3**>(&m_db));
    if (rc != SQLITE_OK) {
        qWarning() << "Cannot open database: " << sqlite3_errmsg(reinterpret_cast<sqlite3*>(m_db));
        return false;
    }

    if (!password.isEmpty()) {
        QString keyPragma = QString("PRAGMA key = '%1';").arg(escapeSqlLiteral(password));
        char* errMsg = nullptr;
        rc = sqlite3_exec(reinterpret_cast<sqlite3*>(m_db), keyPragma.toUtf8().constData(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            qWarning() << "Failed to set key: " << errMsg;
            sqlite3_free(errMsg);
            close();
            return false;
        }
    }

    // Verify key works
    char* errMsg = nullptr;
    rc = sqlite3_exec(reinterpret_cast<sqlite3*>(m_db), "SELECT count(*) FROM sqlite_master;", nullptr, nullptr,
                      &errMsg);
    if (rc != SQLITE_OK) {
        qWarning() << "Invalid password or corrupted database: " << errMsg;
        sqlite3_free(errMsg);
        close();
        return false;
    }

    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    QString databaseError;
    if (!database.execute("PRAGMA foreign_keys = ON;", &databaseError)) {
        qWarning() << "Failed to enable foreign keys";
        close();
        return false;
    }

    int foreignKeysEnabled = 0;
    if (!database.queryInt("PRAGMA foreign_keys;", foreignKeysEnabled, &databaseError) || foreignKeysEnabled != 1) {
        qWarning() << "Failed to verify foreign keys are enabled";
        close();
        return false;
    }

    m_password = password;
    m_isOpen = true;
    if (!initSchema()) {
        close();
        return false;
    }

    cleanupDeadProcessingItems();

    return true;
}

void BookDatabase::cleanupDeadProcessingItems() {
    if (!m_isOpen) return;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db),
                           "SELECT id, processing_id, last_error FROM queue WHERE processing_id > 0 OR state = "
                           "'processing' OR (last_error IS NOT NULL AND last_error != '' AND state != 'error');",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        struct ItemToReset {
            int id = 0;
            QString error;
        };
        QList<ItemToReset> itemsToReset;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            int pid = sqlite3_column_int(stmt, 1);
            const unsigned char* errText = sqlite3_column_text(stmt, 2);
            QString lastError = errText ? QString::fromUtf8(reinterpret_cast<const char*>(errText)) : QString();

            if (pid <= 0) {
                itemsToReset.append({id, lastError});
            } else {
#ifdef Q_OS_UNIX
                if (kill(pid, 0) != 0 && errno == ESRCH) {
                    itemsToReset.append({id, lastError});
                } else {
                    // Process exists, check if it's kllamabooks
                    QFile commFile(QString("/proc/%1/comm").arg(pid));
                    if (commFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        QString comm = QString(commFile.readAll()).trimmed();
                        if (!comm.contains("kllamabooks", Qt::CaseInsensitive)) {
                            itemsToReset.append({id, lastError});
                        }
                    } else {
                        // Could not read comm file, assume dead
                        itemsToReset.append({id, lastError});
                    }
                }
#else
                // Simple fallback for non-Unix if needed, though KllamaBooks is KDE/Linux targeted.
                itemsToReset.append({id, lastError});
#endif
            }
        }
        sqlite3_finalize(stmt);

        for (const auto& item : itemsToReset) {
            if (!item.error.isEmpty()) {
                updateQueueError(item.id, item.error);
            } else {
                updateQueueProcessingId(item.id, 0);
                updateQueueItemState(item.id, "pending");
            }
        }
    }
}

void BookDatabase::close() {
    if (m_db) {
        sqlite3_close(reinterpret_cast<sqlite3*>(m_db));
        m_db = nullptr;
        m_isOpen = false;
    }
    m_password.clear();
}

bool BookDatabase::isOpen() const { return m_isOpen; }

QString BookDatabase::filepath() const { return m_filepath; }

bool BookDatabase::createEncryptedBackup(const QString& backupPath, QString* error) const {
    if (!m_isOpen || !m_db) {
        if (error) *error = "Database is not open";
        return false;
    }
    if (m_filepath.isEmpty() || m_filepath == ":memory:") {
        if (error) *error = "Cannot create file backup of an in-memory database";
        return false;
    }
    if (backupPath.trimmed().isEmpty() || backupPath == m_filepath || QFileInfo(backupPath).isDir()) {
        if (error) *error = "Invalid backup path";
        return false;
    }

    int logFrames = 0;
    int checkpointedFrames = 0;
    const int walRc = sqlite3_wal_checkpoint_v2(reinterpret_cast<sqlite3*>(m_db), nullptr, SQLITE_CHECKPOINT_TRUNCATE,
                                                &logFrames, &checkpointedFrames);
    if (walRc != SQLITE_OK || (logFrames > 0 && checkpointedFrames < logFrames)) {
        if (error) {
            *error = QString("WAL checkpoint incomplete or busy before backup (rc=%1, log=%2, checkpointed=%3): %4")
                         .arg(walRc)
                         .arg(logFrames)
                         .arg(checkpointedFrames)
                         .arg(QString::fromUtf8(sqlite3_errmsg(reinterpret_cast<sqlite3*>(m_db))));
        }
        return false;
    }

    const QString tempPath = backupPath + ".tmp";
    if (!copyFileOwnerOnly(m_filepath, tempPath, error)) {
        return false;
    }

    if (!verifyEncryptedFileIntegrity(tempPath, m_password, error)) {
        QFile::remove(tempPath);
        return false;
    }

    const QString oldBackupPath = backupPath + ".old";
    QFile::remove(oldBackupPath);
    const bool hadExistingBackup = QFile::exists(backupPath);
    if (hadExistingBackup && !QFile::rename(backupPath, oldBackupPath)) {
        QFile::remove(tempPath);
        if (error) *error = "Failed to stage existing backup file for atomic replacement";
        return false;
    }

    if (!QFile::rename(tempPath, backupPath)) {
        QFile::remove(tempPath);
        if (hadExistingBackup) {
            QFile::rename(oldBackupPath, backupPath);
        }
        if (error) *error = "Failed to finalize encrypted backup file";
        return false;
    }
    if (hadExistingBackup) {
        QFile::remove(oldBackupPath);
    }
    if (!QFile::setPermissions(backupPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (error) *error = "Failed to restrict encrypted backup permissions";
        return false;
    }
    return true;
}

bool BookDatabase::restoreEncryptedBackup(const QString& backupPath, const QString& password, bool upgradeSchema,
                                          QString* error) {
    if (m_filepath.isEmpty() || m_filepath == ":memory:") {
        if (error) *error = "Cannot restore file backup into an in-memory database";
        return false;
    }
    if (backupPath.trimmed().isEmpty() || backupPath == m_filepath) {
        if (error) *error = "Invalid backup source path";
        return false;
    }

    if (!verifyEncryptedFileIntegrity(backupPath, password, error)) {
        return false;
    }

    if (m_isOpen && m_db) {
        int logFrames = 0;
        int checkpointedFrames = 0;
        const int walRc = sqlite3_wal_checkpoint_v2(reinterpret_cast<sqlite3*>(m_db), nullptr,
                                                    SQLITE_CHECKPOINT_TRUNCATE, &logFrames, &checkpointedFrames);
        if (walRc != SQLITE_OK || (logFrames > 0 && checkpointedFrames < logFrames)) {
            if (error) {
                *error =
                    QString("WAL checkpoint incomplete or busy before restore (rc=%1, log=%2, checkpointed=%3): %4")
                        .arg(walRc)
                        .arg(logFrames)
                        .arg(checkpointedFrames)
                        .arg(QString::fromUtf8(sqlite3_errmsg(reinterpret_cast<sqlite3*>(m_db))));
            }
            return false;
        }
    }

    const QString tempRestorePath = m_filepath + ".restore.tmp";
    const QString rollbackPath = m_filepath + ".pre_restore.bak";
    const QString rollbackWalPath = rollbackPath + "-wal";
    const QString rollbackShmPath = rollbackPath + "-shm";
    const QString walPath = m_filepath + "-wal";
    const QString shmPath = m_filepath + "-shm";
    QFile::remove(tempRestorePath);
    QFile::remove(rollbackPath);
    QFile::remove(rollbackWalPath);
    QFile::remove(rollbackShmPath);

    if (!copyFileOwnerOnly(backupPath, tempRestorePath, error)) {
        return false;
    }

    const QString previousPassword = m_password;
    const bool wasOpen = m_isOpen;
    const bool hadExistingFile = QFile::exists(m_filepath);
    const bool hadWalFile = QFile::exists(walPath);
    const bool hadShmFile = QFile::exists(shmPath);
    close();

    auto restoreStagedOriginal = [&]() {
        if (hadExistingFile && QFile::exists(rollbackPath) && !QFile::exists(m_filepath)) {
            QFile::rename(rollbackPath, m_filepath);
        }
        if (hadWalFile && QFile::exists(rollbackWalPath) && !QFile::exists(walPath)) {
            QFile::rename(rollbackWalPath, walPath);
        }
        if (hadShmFile && QFile::exists(rollbackShmPath) && !QFile::exists(shmPath)) {
            QFile::rename(rollbackShmPath, shmPath);
        }
        if (wasOpen) {
            open(previousPassword);
        }
    };

    if (hadExistingFile) {
        if (!QFile::rename(m_filepath, rollbackPath)) {
            QFile::remove(tempRestorePath);
            if (wasOpen) open(previousPassword);
            if (error) *error = "Failed to move existing database aside during restore";
            return false;
        }
        QFile::setPermissions(rollbackPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    if (hadWalFile) {
        if (!QFile::rename(walPath, rollbackWalPath)) {
            QFile::remove(tempRestorePath);
            restoreStagedOriginal();
            if (error) *error = "Failed to move existing WAL sidecar aside during restore";
            return false;
        }
        QFile::setPermissions(rollbackWalPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    if (hadShmFile) {
        if (!QFile::rename(shmPath, rollbackShmPath)) {
            QFile::remove(tempRestorePath);
            restoreStagedOriginal();
            if (error) *error = "Failed to move existing SHM sidecar aside during restore";
            return false;
        }
        QFile::setPermissions(rollbackShmPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    if (!QFile::rename(tempRestorePath, m_filepath)) {
        QFile::remove(tempRestorePath);
        restoreStagedOriginal();
        if (error) *error = "Failed to replace database file with backup";
        return false;
    }

    auto rollbackReplacedFile = [&]() {
        close();
        QFile::remove(m_filepath);
        QFile::remove(walPath);
        QFile::remove(shmPath);
        restoreStagedOriginal();
    };

    if (!QFile::setPermissions(m_filepath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        rollbackReplacedFile();
        if (error) *error = "Failed to restrict restored database permissions";
        return false;
    }

    if (upgradeSchema) {
        if (!open(password)) {
            rollbackReplacedFile();
            if (error) *error = "Failed to open and upgrade restored backup";
            return false;
        }
        QFile::remove(rollbackPath);
        QFile::remove(rollbackWalPath);
        QFile::remove(rollbackShmPath);
        return true;
    }

    // Open restored database at its exact schema version without running forward migrations
    int rc = sqlite3_open(m_filepath.toUtf8().constData(), reinterpret_cast<sqlite3**>(&m_db));
    if (rc != SQLITE_OK) {
        rollbackReplacedFile();
        if (error) *error = "Failed to open restored database file";
        return false;
    }

    if (!password.isEmpty()) {
        const QByteArray keySql = QString("PRAGMA key = '%1';").arg(escapeSqlLiteral(password)).toUtf8();
        if (sqlite3_exec(reinterpret_cast<sqlite3*>(m_db), keySql.constData(), nullptr, nullptr, nullptr) !=
            SQLITE_OK) {
            rollbackReplacedFile();
            if (error) *error = "Failed to set key on restored database";
            return false;
        }
    }

    if (sqlite3_exec(reinterpret_cast<sqlite3*>(m_db), "SELECT count(*) FROM sqlite_master;", nullptr, nullptr,
                     nullptr) != SQLITE_OK) {
        rollbackReplacedFile();
        if (error) *error = "Failed to decrypt restored database with provided password";
        return false;
    }

    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    if (!database.execute("PRAGMA foreign_keys = ON;", error)) {
        rollbackReplacedFile();
        return false;
    }
    int fkEnabled = 0;
    if (!database.queryInt("PRAGMA foreign_keys;", fkEnabled, error) || fkEnabled != 1) {
        rollbackReplacedFile();
        if (error) *error = "Failed to verify foreign keys on restored database";
        return false;
    }

    m_password = password;
    m_isOpen = true;
    QFile::remove(rollbackPath);
    QFile::remove(rollbackWalPath);
    QFile::remove(rollbackShmPath);
    return true;
}

bool BookDatabase::initSchema() {
    if (!m_isOpen) return false;

    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    db::MigrationRunner runner = db::MigrationFactory::createRunner();

    // Enforce a recoverable encrypted backup before migrating any existing file-backed user book
    if (!m_filepath.isEmpty() && m_filepath != ":memory:") {
        const auto& migrations = runner.getMigrations();
        const int targetVersion = migrations.empty() ? 0 : migrations.back().toVersion;

        int userVersion = 0;
        database.queryInt("PRAGMA user_version;", userVersion);

        int tableCount = 0;
        database.queryInt("SELECT COUNT(*) FROM sqlite_master;", tableCount);

        int schemaTableVersion = 0;
        int hasSchemaTable = 0;
        database.queryInt("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='schema_version';",
                          hasSchemaTable);
        if (hasSchemaTable > 0) {
            database.queryInt("SELECT COALESCE(MAX(version), 0) FROM schema_version;", schemaTableVersion);
        }

        const int currentVersion = qMax(userVersion, schemaTableVersion);
        if (targetVersion > 0 && currentVersion < targetVersion && (currentVersion > 0 || tableCount > 0)) {
            const QString backupPath = QString("%1.pre_v%2.bak").arg(m_filepath).arg(targetVersion);
            QString backupError;
            if (!createEncryptedBackup(backupPath, &backupError)) {
                qWarning() << "Pre-migration encrypted backup failed:" << backupError;
                return false;
            }
        }
    }

    QString error;
    if (!runner.run(database, &error)) {
        qWarning() << "Migration failed:" << error;
        return false;
    }

    return true;
}

void BookDatabase::setSetting(const QString& scope, int targetId, const QString& key, const QString& value) {
    if (!m_isOpen) return;

    const char* sql = "INSERT OR REPLACE INTO settings (scope, target_id, key, value) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, scope.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, targetId);
    sqlite3_bind_text(stmt, 3, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, value.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

QString BookDatabase::getSetting(const QString& scope, int targetId, const QString& key,
                                 const QString& defaultValue) const {
    if (!m_isOpen) return defaultValue;

    const char* sql = "SELECT value FROM settings WHERE scope = ? AND target_id = ? AND key = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return defaultValue;

    sqlite3_bind_text(stmt, 1, scope.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, targetId);
    sqlite3_bind_text(stmt, 3, key.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    QString result = defaultValue;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool BookDatabase::moveItem(const store::ItemRef& item, int newFolderId) {
    if (!m_isOpen) return false;
    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    store::FolderStore folderStore(database);
    return folderStore.moveItem(item, newFolderId).isSuccess();
}

bool BookDatabase::moveFolder(int id, int newParentId) {
    if (!m_isOpen) return false;
    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    store::FolderStore folderStore(database);
    return folderStore.moveFolder(id, newParentId).isSuccess();
}

int BookDatabase::addMessage(int parentId, const QString& content, const QString& role, int folderId) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO messages (parent_id, folder_id, role, content) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, parentId);
    sqlite3_bind_int(stmt, 2, folderId);
    sqlite3_bind_text(stmt, 3, role.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid(reinterpret_cast<sqlite3*>(m_db));
    sqlite3_finalize(stmt);
    return id;
}

int BookDatabase::addDocumentMerge(int documentId, const QString& sourceDocumentIds, const QString& prompt,
                                   const QString& model, int versionHistoryId) {
    if (!m_isOpen) return -1;
    const char* sql =
        "INSERT INTO document_merges (document_id, source_document_ids, version_history_id) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, documentId);
    sqlite3_bind_text(stmt, 2, sourceDocumentIds.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, versionHistoryId);

    int rc = sqlite3_step(stmt);
    int id = (rc == SQLITE_DONE) ? sqlite3_last_insert_rowid(reinterpret_cast<sqlite3*>(m_db)) : -1;
    sqlite3_finalize(stmt);
    return id;
}

std::optional<BookDatabase::DocumentMergeEntry> BookDatabase::getDocumentMerge(int documentId) const {
    if (!m_isOpen) return std::nullopt;

    QString sqlStr =
        "SELECT dm.id, dm.document_id, dm.source_document_ids, ph.prompt, ph.model, dm.timestamp, "
        "dm.version_history_id "
        "FROM document_merges dm "
        "LEFT JOIN prompt_history ph ON dm.document_id = ph.document_id "
        "WHERE dm.document_id = ? "
        "ORDER BY ph.timestamp DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sqlStr.toUtf8().constData(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, documentId);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        DocumentMergeEntry entry;
        entry.id = sqlite3_column_int(stmt, 0);
        entry.documentId = sqlite3_column_int(stmt, 1);
        entry.sourceDocumentIds = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        entry.prompt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        entry.model = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
        entry.timestamp = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
        entry.versionHistoryId = sqlite3_column_int(stmt, 6);

        sqlite3_finalize(stmt);
        return entry;
    }
    sqlite3_finalize(stmt);
    return std::nullopt;
}

bool BookDatabase::updateDocumentMergeVersion(int mergeId, int versionHistoryId) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE document_merges SET version_history_id = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, versionHistoryId);
    sqlite3_bind_int(stmt, 2, mergeId);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::addDocumentHistory(int documentId, const QString& actionType, const QString& content) {
    return addDocumentHistoryReturningId(documentId, actionType, content) != -1;
}

int BookDatabase::addDocumentHistoryReturningId(int documentId, const QString& actionType, const QString& content) {
    if (!m_isOpen) return -1;

    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Document, documentId);
    if (!artIdOpt.has_value()) return -1;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto curVer = store.getCurrentVersion(*artIdOpt);
    if (!curVer.has_value()) return -1;

    QJsonObject metaObj;
    metaObj["action_type"] = actionType;
    const QByteArray metaUtf8 = QJsonDocument(metaObj).toJson(QJsonDocument::Compact);

    const char* sql =
        "INSERT INTO artifact_versions (artifact_id, parent_id, title, content, metadata, is_sealed) "
        "VALUES (?, ?, ?, ?, ?, 1) RETURNING id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(rawDb, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, *artIdOpt);
    if (curVer->parentId.has_value()) {
        sqlite3_bind_int(stmt, 2, *curVer->parentId);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    const QByteArray titleUtf8 = curVer->title.toUtf8();
    const QByteArray contentUtf8 = content.toUtf8();
    sqlite3_bind_text(stmt, 3, titleUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, contentUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, metaUtf8.constData(), -1, SQLITE_TRANSIENT);

    int histVersionId = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        histVersionId = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (histVersionId > 0 && !curVer->isSealed) {
        database.execute(
            QString("UPDATE artifact_versions SET parent_id = %1 WHERE id = %2;").arg(histVersionId).arg(curVer->id));
    }

    return histVersionId;
}

QList<BookDatabase::DocumentHistoryEntry> BookDatabase::getDocumentHistory(int documentId) const {
    QList<DocumentHistoryEntry> items;
    if (!m_isOpen) return items;

    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Document, documentId);
    if (!artIdOpt.has_value()) return items;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto histRes = store.getVersionHistory(*artIdOpt);
    if (!histRes.isSuccess()) return items;

    for (const auto& ver : *histRes.value) {
        if (!ver.isSealed) continue;
        DocumentHistoryEntry entry;
        entry.id = ver.id;
        entry.content = ver.content;
        entry.timestamp = ver.createdAt;
        entry.actionType = QStringLiteral("sealed_version");

        if (!ver.metadata.isEmpty()) {
            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(ver.metadata.toUtf8(), &parseErr);
            if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
                const QString act = doc.object().value("action_type").toString();
                if (!act.isEmpty()) entry.actionType = act;
            }
        }
        items.append(entry);
    }
    return items;
}

bool BookDatabase::restoreDocumentHistoryVersion(int documentId, int historyVersionId) {
    if (!m_isOpen) return false;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Document, documentId);
    if (!artIdOpt.has_value()) return false;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto curVer = store.getCurrentVersion(*artIdOpt);
    if (!curVer.has_value()) return false;

    if (!curVer->isSealed) {
        if (!store.sealVersion(curVer->id).isSuccess()) return false;
    }
    return store.restoreVersion(curVer->id, historyVersionId).isSuccess();
}

int BookDatabase::getArtifactCurrentVersionId(int id, const QString& targetType) const {
    if (!m_isOpen) return 0;
    auto kindOpt = parseArtifactKind(targetType);
    if (!kindOpt.has_value()) return 0;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, *kindOpt, id);
    if (!artIdOpt.has_value()) return 0;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto art = store.getArtifact(*artIdOpt);
    return art.has_value() ? art->currentVersionId : 0;
}

QString BookDatabase::getDraftForkedSourceContent(int draftId) const {
    if (!m_isOpen) return QString();
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto draftArtId = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Draft, draftId);
    if (!draftArtId.has_value()) return QString();

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto curVer = store.getCurrentVersion(*draftArtId);
    while (curVer.has_value()) {
        if (curVer->forkedFromVersionId.has_value()) {
            auto srcVer = store.getVersion(*curVer->forkedFromVersionId);
            if (srcVer.has_value()) return srcVer->content;
            break;
        }
        if (!curVer->parentId.has_value()) break;
        curVer = store.getVersion(*curVer->parentId);
    }
    return QString();
}

bool BookDatabase::updateMessage(int id, const QString& newContent) {
    if (!m_isOpen) return false;

    const char* sql = "UPDATE messages SET content = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, newContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::deleteMessage(int id) {
    if (!m_isOpen) return false;

    // Recursively delete children? For now just delete this one.
    const char* sql = "DELETE FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int BookDatabase::getRootMessageId(int messageId) const {
    if (!m_isOpen || messageId <= 0) return messageId;

    int currentId = messageId;
    const char* sql = "SELECT parent_id FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return currentId;
    }

    while (currentId > 0) {
        int parentId = 0;
        sqlite3_bind_int(stmt, 1, currentId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            parentId = sqlite3_column_int(stmt, 0);
        }
        sqlite3_reset(stmt);

        if (parentId <= 0) {
            break;
        }
        currentId = parentId;
    }
    sqlite3_finalize(stmt);
    return currentId;
}

QString BookDatabase::getInheritedSetting(int messageId, const QString& key) const {
    if (!m_isOpen) return QString();

    int currentId = messageId;
    const char* sql = "SELECT parent_id FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return QString();
    }

    QString inheritedValue;
    while (currentId > 0) {
        QString currentValue = "";
        ChatNode c = getChat(currentId);
        if (key == "title")
            currentValue = c.title;
        else if (key == "systemPrompt")
            currentValue = c.systemPrompt;
        else if (key == "sendBehavior")
            currentValue = c.sendBehavior;
        else if (key == "model")
            currentValue = c.model;
        else if (key == "multiLine")
            currentValue = c.multiLine;
        else if (key == "draftPrompt")
            currentValue = c.draftPrompt;
        else if (key == "userNote")
            currentValue = c.userNote;
        if (!currentValue.isEmpty() && currentValue != "default") {
            inheritedValue = currentValue;
            break;
        }

        int parentId = 0;
        sqlite3_bind_int(stmt, 1, currentId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            parentId = sqlite3_column_int(stmt, 0);
        }
        sqlite3_reset(stmt);

        if (parentId <= 0) {
            break;
        }
        currentId = parentId;
    }
    sqlite3_finalize(stmt);
    return inheritedValue;
}

std::optional<MessageNode> BookDatabase::getMessage(int id) const {
    if (!m_isOpen) return std::nullopt;

    QString sqlStr =
        "SELECT id, parent_id, folder_id, role, content, timestamp, is_expanded FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sqlStr.toUtf8().constData(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);

    std::optional<MessageNode> result = std::nullopt;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        MessageNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.parentId = sqlite3_column_int(stmt, 1);
        node.folderId = sqlite3_column_int(stmt, 2);
        const unsigned char* roleText = sqlite3_column_text(stmt, 3);
        node.role = roleText ? QString::fromUtf8(reinterpret_cast<const char*>(roleText)) : "";
        const unsigned char* contentText = sqlite3_column_text(stmt, 4);
        node.content = contentText ? QString::fromUtf8(reinterpret_cast<const char*>(contentText)) : "";
        const unsigned char* timestampText = sqlite3_column_text(stmt, 5);
        QString tsStr = timestampText ? QString::fromUtf8(reinterpret_cast<const char*>(timestampText)) : "";
        node.timestamp = QDateTime::fromString(tsStr, Qt::ISODate);
        node.isExpanded = sqlite3_column_int(stmt, 6) != 0;
        result = node;
    }

    sqlite3_finalize(stmt);
    return result;
}

QList<MessageNode> BookDatabase::getMessages() const {
    QList<MessageNode> nodes;
    if (!m_isOpen) return nodes;

    const char* sql =
        "SELECT id, parent_id, folder_id, role, content, timestamp, is_expanded FROM messages ORDER BY id;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return nodes;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MessageNode node;
        node.id = sqlite3_column_int(stmt, 0);
        node.parentId = sqlite3_column_int(stmt, 1);
        node.folderId = sqlite3_column_int(stmt, 2);
        node.role = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        node.content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
        QString ts = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
        node.timestamp = QDateTime::fromString(ts, Qt::ISODate);
        node.isExpanded = sqlite3_column_int(stmt, 6) != 0;
        nodes.append(node);
    }
    sqlite3_finalize(stmt);
    return nodes;
}

int BookDatabase::addDocument(int folderId, const QString& title, const QString& content, int parentId,
                              const QString& metadata) {
    if (!m_isOpen) return -1;

    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    db::Database database(rawDb);
    store::ArtifactStore store(database);

    auto res = store.createArtifact(store::ArtifactKind::Document, folderId, title, content, metadata);
    if (!res.isSuccess()) return -1;

    if (parentId > 0) {
        auto parentArtId = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Document, parentId);
        if (parentArtId.has_value()) {
            auto parentVer = store.getCurrentVersion(*parentArtId);
            if (parentVer.has_value()) {
                if (!parentVer->isSealed) {
                    store.sealVersion(parentVer->id);
                }
                database.execute(QString("UPDATE artifact_versions SET forked_from_version_id = %1 WHERE id = %2;")
                                     .arg(parentVer->id)
                                     .arg(res.value->id));
            }
        }
    }
    return res.value->artifactId;
}

bool BookDatabase::updateDocument(int id, const QString& newTitle, const QString& newContent, const QString& metadata) {
    if (!m_isOpen) return false;

    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Document, id);
    if (!artIdOpt.has_value()) return false;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto curVer = store.getCurrentVersion(*artIdOpt);
    if (!curVer.has_value()) return false;

    const QString effectiveMeta = metadata.isEmpty() ? curVer->metadata : metadata;
    return updateOrAdvanceArtifact(database, *artIdOpt, newTitle, newContent, effectiveMeta);
}

std::optional<DocumentNode> BookDatabase::getDocument(int id) const {
    if (!m_isOpen) return std::nullopt;

    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Document, id);
    if (!artIdOpt.has_value()) return std::nullopt;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto art = store.getArtifact(*artIdOpt);
    auto ver = store.getCurrentVersion(*artIdOpt);
    if (!art.has_value() || !ver.has_value()) return std::nullopt;

    DocumentNode node;
    node.id = art->id;
    node.folderId = art->folderId;
    node.title = ver->title;
    node.content = ver->content;
    node.timestamp = QDateTime::fromString(ver->createdAt, Qt::ISODate);
    node.parentId = findParentDocumentArtifactId(rawDb, art->id);
    node.metadata = ver->metadata;
    if (node.metadata.isEmpty()) {
        node.metadata = getSetting("document", node.id, "metadata");
    }
    return node;
}

QList<DocumentNode> BookDatabase::getDocuments(int folderId) const {
    QList<DocumentNode> nodes;
    if (!m_isOpen) return nodes;

    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto artsRes =
        store.getArtifacts(store::ArtifactKind::Document, folderId == -1 ? std::nullopt : std::optional<int>(folderId));
    if (!artsRes.isSuccess()) return nodes;

    for (const auto& art : *artsRes.value) {
        auto ver = store.getVersion(art.currentVersionId);
        if (!ver.has_value()) continue;
        DocumentNode node;
        node.id = art.id;
        node.folderId = art.folderId;
        node.title = ver->title;
        node.content = ver->content;
        node.timestamp = QDateTime::fromString(ver->createdAt, Qt::ISODate);
        node.parentId = findParentDocumentArtifactId(rawDb, art.id);
        node.metadata = ver->metadata;
        if (node.metadata.isEmpty()) {
            node.metadata = getSetting("document", node.id, "metadata");
        }
        nodes.append(node);
    }
    return nodes;
}

bool BookDatabase::deleteDocument(int id) {
    if (!m_isOpen) return false;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Document, id);
    if (!artIdOpt.has_value()) return false;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    return store.deleteArtifact(*artIdOpt).isSuccess();
}

int BookDatabase::addNote(int folderId, const QString& title, const QString& content) {
    if (!m_isOpen) return -1;
    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    store::ArtifactStore store(database);
    auto res = store.createArtifact(store::ArtifactKind::Note, folderId, title, content, QString(""));
    return res.isSuccess() ? res.value->artifactId : -1;
}

bool BookDatabase::updateNote(int id, const QString& newTitle, const QString& newContent) {
    if (!m_isOpen) return false;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Note, id);
    if (!artIdOpt.has_value()) return false;

    db::Database database(rawDb);
    return updateOrAdvanceArtifact(database, *artIdOpt, newTitle, newContent, QString(""));
}

std::optional<NoteNode> BookDatabase::getNote(int id) const {
    if (!m_isOpen) return std::nullopt;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Note, id);
    if (!artIdOpt.has_value()) return std::nullopt;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto art = store.getArtifact(*artIdOpt);
    auto ver = store.getCurrentVersion(*artIdOpt);
    if (!art.has_value() || !ver.has_value()) return std::nullopt;

    NoteNode node;
    node.id = art->id;
    node.folderId = art->folderId;
    node.parentId = 0;
    node.title = ver->title;
    node.content = ver->content;
    node.timestamp = QDateTime::fromString(ver->createdAt, Qt::ISODate);
    return node;
}

QList<NoteNode> BookDatabase::getNotes(int folderId) const {
    QList<NoteNode> nodes;
    if (!m_isOpen) return nodes;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto artsRes =
        store.getArtifacts(store::ArtifactKind::Note, folderId == -1 ? std::nullopt : std::optional<int>(folderId));
    if (!artsRes.isSuccess()) return nodes;

    for (const auto& art : *artsRes.value) {
        auto ver = store.getVersion(art.currentVersionId);
        if (!ver.has_value()) continue;
        NoteNode node;
        node.id = art.id;
        node.folderId = art.folderId;
        node.parentId = 0;
        node.title = ver->title;
        node.content = ver->content;
        node.timestamp = QDateTime::fromString(ver->createdAt, Qt::ISODate);
        nodes.append(node);
    }
    return nodes;
}

bool BookDatabase::deleteNote(int id) {
    if (!m_isOpen) return false;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Note, id);
    if (!artIdOpt.has_value()) return false;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    return store.deleteArtifact(*artIdOpt).isSuccess();
}

int BookDatabase::addTemplate(int folderId, const QString& title, const QString& content) {
    if (!m_isOpen) return -1;
    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    store::ArtifactStore store(database);
    auto res = store.createArtifact(store::ArtifactKind::Template, folderId, title, content, QString(""));
    return res.isSuccess() ? res.value->artifactId : -1;
}

bool BookDatabase::updateTemplate(int id, const QString& newTitle, const QString& newContent) {
    if (!m_isOpen) return false;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Template, id);
    if (!artIdOpt.has_value()) return false;

    db::Database database(rawDb);
    return updateOrAdvanceArtifact(database, *artIdOpt, newTitle, newContent, QString(""));
}

QList<DocumentNode> BookDatabase::getTemplates(int folderId) const {
    QList<DocumentNode> nodes;
    if (!m_isOpen) return nodes;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto artsRes =
        store.getArtifacts(store::ArtifactKind::Template, folderId == -1 ? std::nullopt : std::optional<int>(folderId));
    if (!artsRes.isSuccess()) return nodes;

    for (const auto& art : *artsRes.value) {
        auto ver = store.getVersion(art.currentVersionId);
        if (!ver.has_value()) continue;
        DocumentNode node;
        node.id = art.id;
        node.folderId = art.folderId;
        node.title = ver->title;
        node.content = ver->content;
        node.timestamp = QDateTime::fromString(ver->createdAt, Qt::ISODate);
        node.parentId = 0;
        nodes.append(node);
    }
    return nodes;
}

std::optional<DocumentNode> BookDatabase::getTemplate(int id) const {
    if (!m_isOpen) return std::nullopt;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Template, id);
    if (!artIdOpt.has_value()) return std::nullopt;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto art = store.getArtifact(*artIdOpt);
    auto ver = store.getCurrentVersion(*artIdOpt);
    if (!art.has_value() || !ver.has_value()) return std::nullopt;

    DocumentNode node;
    node.id = art->id;
    node.folderId = art->folderId;
    node.title = ver->title;
    node.content = ver->content;
    node.timestamp = QDateTime::fromString(ver->createdAt, Qt::ISODate);
    node.parentId = 0;
    return node;
}

int BookDatabase::addDraft(int folderId, const QString& title, const QString& content, int parentId,
                           const QString& targetType) {
    if (!m_isOpen) return -1;

    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    db::Database database(rawDb);
    store::ArtifactStore store(database);

    QJsonObject metaObj;
    metaObj["target_type"] = targetType.isEmpty() ? QStringLiteral("document") : targetType;
    if (parentId > 0) {
        metaObj["parent_id"] = parentId;
    }
    const QString metaJson = QString::fromUtf8(QJsonDocument(metaObj).toJson(QJsonDocument::Compact));

    auto res = store.createArtifact(store::ArtifactKind::Draft, folderId, title, content, metaJson);
    if (!res.isSuccess()) return -1;

    if (parentId > 0) {
        auto parentKindOpt = parseArtifactKind(targetType);
        if (parentKindOpt.has_value()) {
            auto parentArtId = resolveArtifactIdForKind(rawDb, *parentKindOpt, parentId);
            if (parentArtId.has_value()) {
                auto parentVer = store.getCurrentVersion(*parentArtId);
                if (parentVer.has_value()) {
                    if (!parentVer->isSealed) {
                        store.sealVersion(parentVer->id);
                    }
                    database.execute(QString("UPDATE artifact_versions SET forked_from_version_id = %1 WHERE id = %2;")
                                         .arg(parentVer->id)
                                         .arg(res.value->id));
                }
            }
        }
    }
    return res.value->artifactId;
}

bool BookDatabase::updateDraft(int id, const QString& newTitle, const QString& newContent) {
    if (!m_isOpen) return false;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Draft, id);
    if (!artIdOpt.has_value()) return false;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto curVer = store.getCurrentVersion(*artIdOpt);
    if (!curVer.has_value()) return false;

    return updateOrAdvanceArtifact(database, *artIdOpt, newTitle, newContent, curVer->metadata);
}

QList<DocumentNode> BookDatabase::getDrafts(int folderId) const {
    QList<DocumentNode> nodes;
    if (!m_isOpen) return nodes;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto artsRes =
        store.getArtifacts(store::ArtifactKind::Draft, folderId == -1 ? std::nullopt : std::optional<int>(folderId));
    if (!artsRes.isSuccess()) return nodes;

    for (const auto& art : *artsRes.value) {
        if (auto dOpt = getDraft(art.id)) {
            nodes.append(*dOpt);
        }
    }
    return nodes;
}

std::optional<DocumentNode> BookDatabase::getDraft(int id) const {
    if (!m_isOpen) return std::nullopt;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Draft, id);
    if (!artIdOpt.has_value()) return std::nullopt;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto art = store.getArtifact(*artIdOpt);
    auto ver = store.getCurrentVersion(*artIdOpt);
    if (!art.has_value() || !ver.has_value()) return std::nullopt;

    DocumentNode node;
    node.id = art->id;
    node.folderId = art->folderId;
    node.title = ver->title;
    node.content = ver->content;
    node.timestamp = QDateTime::fromString(ver->createdAt, Qt::ISODate);
    node.parentId = 0;
    node.targetType = QStringLiteral("document");

    auto srcRes = store.resolveSourceArtifact(art->id);
    if (srcRes.isSuccess()) {
        node.parentId = srcRes.value->id;
        node.targetType = artifactKindString(srcRes.value->kind);
    } else if (!ver->metadata.isEmpty()) {
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(ver->metadata.toUtf8(), &parseErr);
        if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
            if (obj.contains("target_type")) node.targetType = obj.value("target_type").toString("document");
            if (obj.contains("parent_id")) node.parentId = obj.value("parent_id").toInt(0);
        }
    }
    return node;
}

bool BookDatabase::deleteDraft(int id) {
    if (!m_isOpen) return false;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Draft, id);
    if (!artIdOpt.has_value()) return false;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    return store.deleteArtifact(*artIdOpt).isSuccess();
}

int BookDatabase::addFolder(int parentId, const QString& name, const QString& type, bool isExpanded) {
    if (!m_isOpen) return -1;
    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    store::FolderStore folderStore(database);
    auto res = folderStore.createFolder(parentId, name, type, isExpanded);
    return res.isSuccess() ? *res.value : -1;
}

bool BookDatabase::updateFolder(int id, const QString& newName) {
    if (!m_isOpen) return false;
    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    store::FolderStore folderStore(database);
    return folderStore.renameFolder(id, newName).isSuccess();
}

bool BookDatabase::deleteFolder(int id) {
    if (!m_isOpen) return false;
    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    store::FolderStore folderStore(database);
    return folderStore.deleteFolder(id).isSuccess();
}

void BookDatabase::setFolderExpanded(int id, bool expanded) {
    if (!m_isOpen) return;
    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    store::FolderStore folderStore(database);
    folderStore.setExpanded(id, expanded);
}

void BookDatabase::setMessageExpanded(int id, bool expanded) {
    if (!m_isOpen) return;
    const char* sql = "UPDATE messages SET is_expanded = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, expanded ? 1 : 0);
    sqlite3_bind_int(stmt, 2, id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

QList<FolderNode> BookDatabase::getFolders(const QString& type) const {
    QList<FolderNode> folderNodes;
    if (!m_isOpen) return folderNodes;

    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    store::FolderStore folderStore(database);
    auto res = folderStore.getFolders(type);
    if (!res.isSuccess()) return folderNodes;

    for (const auto& rec : *res.value) {
        FolderNode node;
        node.id = rec.id;
        node.parentId = rec.parentId;
        node.name = rec.name;
        node.type = rec.rootType;
        node.timestamp = rec.timestamp;
        node.position = rec.position;
        node.isExpanded = rec.isExpanded;
        folderNodes.append(node);
    }
    return folderNodes;
}

int BookDatabase::enqueuePrompt(int messageId, const QString& model, const QString& prompt, int priority,
                                const QString& targetType, int parentId, const QString& targetAction) {
    if (!m_isOpen) return -1;

    const char* sql =
        "INSERT INTO queue (message_id, model, prompt, priority, target_type, state, parent_id, target_action) VALUES "
        "(?, ?, ?, ?, ?, 'pending', ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, messageId);
    sqlite3_bind_text(stmt, 2, model.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, prompt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, priority);
    sqlite3_bind_text(stmt, 5, targetType.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, parentId);
    sqlite3_bind_text(stmt, 7, targetAction.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid(reinterpret_cast<sqlite3*>(m_db));
    sqlite3_finalize(stmt);
    if (targetType == "document") {
        const char* ph_sql = "INSERT INTO prompt_history (document_id, prompt, model, queue_id) VALUES (?, ?, ?, ?);";
        sqlite3_stmt* ph_stmt;
        if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), ph_sql, -1, &ph_stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(ph_stmt, 1, messageId);
            sqlite3_bind_text(ph_stmt, 2, prompt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(ph_stmt, 3, model.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(ph_stmt, 4, id);
            sqlite3_step(ph_stmt);
            sqlite3_finalize(ph_stmt);
        }
    }
    return id;
}

bool BookDatabase::updateQueueItemState(int id, const QString& state, const QString& response) {
    if (!m_isOpen) return false;

    // If response is empty, maybe don't overwrite an existing response unless explicitly asked?
    // Wait, the prompt said response has a default of "". When we just update state to 'processing',
    // it will overwrite response with "". This is fine because when it starts processing, response is empty anyway.
    QString sqlStr = "UPDATE queue SET state = ?, response = ?";
    if (state != "error") {
        sqlStr += ", last_error = ''";
    }
    sqlStr += " WHERE id = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sqlStr.toUtf8().constData(), -1, &stmt, nullptr) !=
        SQLITE_OK)
        return false;

    sqlite3_bind_text(stmt, 1, state.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, response.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

BookDatabase::QueueStats BookDatabase::getQueueStats() const {
    QueueStats stats;
    if (!m_isOpen) return stats;

    const char* sql = "SELECT state, COUNT(*) FROM queue GROUP BY state;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return stats;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QString state = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        int count = sqlite3_column_int(stmt, 1);

        if (state == "pending") {
            stats.pending = count;
        } else if (state == "processing") {
            stats.processing = count;
        } else if (state == "completed") {
            stats.completed = count;
        } else if (state == "error") {
            stats.error = count;
        }
    }

    sqlite3_finalize(stmt);
    return stats;
}

bool BookDatabase::isGenerating(int targetId, const QString& targetType, const QString& targetAction) const {
    if (!m_isOpen) return false;

    sqlite3_stmt* stmt;

    if (targetAction.isNull() || targetAction.isEmpty()) {
        const char* sql =
            "SELECT COUNT(*) FROM queue WHERE message_id = ? AND target_type = ? AND state IN "
            "('pending', 'processing');";
        if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_int(stmt, 1, targetId);
        sqlite3_bind_text(stmt, 2, targetType.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    } else {
        const char* sql =
            "SELECT COUNT(*) FROM queue WHERE message_id = ? AND target_type = ? AND target_action = ? AND state IN "
            "('pending', 'processing');";
        if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_int(stmt, 1, targetId);
        sqlite3_bind_text(stmt, 2, targetType.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, targetAction.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    }

    bool result = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<QueueItem> BookDatabase::getQueue() const {
    QList<QueueItem> items;
    if (!m_isOpen) return items;

    const char* sql =
        "SELECT id, message_id, model, prompt, processing_id, last_error, priority, created_at, target_type, state, "
        "response, parent_id, target_action FROM queue ORDER BY priority "
        "DESC, "
        "created_at DESC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return items;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QueueItem item;
        item.id = sqlite3_column_int(stmt, 0);
        item.messageId = sqlite3_column_int(stmt, 1);
        item.model = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        item.prompt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        item.processingId = sqlite3_column_int(stmt, 4);
        const unsigned char* errorText = sqlite3_column_text(stmt, 5);
        if (errorText) item.lastError = QString::fromUtf8(reinterpret_cast<const char*>(errorText));
        item.priority = sqlite3_column_int(stmt, 6);
        item.timestamp = QDateTime::fromString(
            QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7))), Qt::ISODate);
        item.targetType = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
        if (item.targetType.isEmpty()) item.targetType = "message";
        const unsigned char* stateText = sqlite3_column_text(stmt, 9);
        if (stateText) item.state = QString::fromUtf8(reinterpret_cast<const char*>(stateText));
        const unsigned char* responseText = sqlite3_column_text(stmt, 10);
        if (responseText) item.response = QString::fromUtf8(reinterpret_cast<const char*>(responseText));
        item.parentId = sqlite3_column_int(stmt, 11);
        const unsigned char* actionText = sqlite3_column_text(stmt, 12);
        if (actionText) item.targetAction = QString::fromUtf8(reinterpret_cast<const char*>(actionText));

        items.append(item);
    }

    sqlite3_finalize(stmt);
    return items;
}

std::optional<QueueItem> BookDatabase::getQueueItem(int id) const {
    if (!m_isOpen) return std::nullopt;

    const char* sql =
        "SELECT id, message_id, model, prompt, processing_id, last_error, priority, created_at, target_type, state, "
        "response, parent_id, target_action FROM queue WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);

    std::optional<QueueItem> result = std::nullopt;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        QueueItem item;
        item.id = sqlite3_column_int(stmt, 0);
        item.messageId = sqlite3_column_int(stmt, 1);
        item.model = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        item.prompt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        item.processingId = sqlite3_column_int(stmt, 4);
        const unsigned char* errorText = sqlite3_column_text(stmt, 5);
        if (errorText) item.lastError = QString::fromUtf8(reinterpret_cast<const char*>(errorText));
        item.priority = sqlite3_column_int(stmt, 6);
        item.timestamp = QDateTime::fromString(
            QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7))), Qt::ISODate);
        item.targetType = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)));
        if (item.targetType.isEmpty()) item.targetType = "message";
        const unsigned char* stateText = sqlite3_column_text(stmt, 9);
        if (stateText) item.state = QString::fromUtf8(reinterpret_cast<const char*>(stateText));
        const unsigned char* responseText = sqlite3_column_text(stmt, 10);
        if (responseText) item.response = QString::fromUtf8(reinterpret_cast<const char*>(responseText));
        item.parentId = sqlite3_column_int(stmt, 11);
        const unsigned char* actionText = sqlite3_column_text(stmt, 12);
        if (actionText) item.targetAction = QString::fromUtf8(reinterpret_cast<const char*>(actionText));

        result = item;
    }
    sqlite3_finalize(stmt);
    return result;
}

bool BookDatabase::updateQueueProcessingId(int id, int processingId) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE queue SET processing_id = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, processingId);
    sqlite3_bind_int(stmt, 2, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::updateQueueError(int id, const QString& error) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE queue SET last_error = ?, processing_id = 0, state = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    QString state = error.isEmpty() ? "pending" : "error";

    sqlite3_bind_text(stmt, 1, error.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, state.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::updateQueueItemPrompt(int id, const QString& prompt) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE queue SET prompt = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, prompt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::updateQueueItemModel(int id, const QString& model) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE queue SET model = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, model.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::updateQueueItemPriority(int id, int priority) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE queue SET priority = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, priority);
    sqlite3_bind_int(stmt, 2, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::deleteQueueItem(int id) {
    if (!m_isOpen) return false;
    const char* sql = "DELETE FROM queue WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int BookDatabase::addNotification(int targetId, const QString& targetType, const QString& type) {
    if (!m_isOpen) return -1;
    const char* sql = "INSERT INTO notifications (target_id, target_type, type) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, targetId);
    sqlite3_bind_text(stmt, 2, targetType.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, type.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int id = sqlite3_last_insert_rowid(reinterpret_cast<sqlite3*>(m_db));
    sqlite3_finalize(stmt);
    return id;
}

QList<Notification> BookDatabase::getNotifications(bool includeDismissed) const {
    QList<Notification> notifications;
    if (!m_isOpen) return notifications;
    QString sql = "SELECT id, target_id, target_type, type, is_dismissed, created_at FROM notifications";
    if (!includeDismissed) sql += " WHERE is_dismissed = 0";
    sql += " ORDER BY created_at DESC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql.toUtf8().constData(), -1, &stmt, nullptr) != SQLITE_OK)
        return notifications;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Notification n;
        n.id = sqlite3_column_int(stmt, 0);
        n.targetId = sqlite3_column_int(stmt, 1);
        n.targetType = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        n.type = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        n.isDismissed = sqlite3_column_int(stmt, 4) != 0;
        n.timestamp = QDateTime::fromString(
            QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5))), Qt::ISODate);
        notifications.append(n);
    }
    sqlite3_finalize(stmt);
    return notifications;
}

bool BookDatabase::dismissNotification(int id) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE notifications SET is_dismissed = 1 WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::dismissNotificationByTarget(int targetId, const QString& targetType) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE notifications SET is_dismissed = 1 WHERE target_id = ? AND target_type = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, targetId);
    sqlite3_bind_text(stmt, 2, targetType.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool BookDatabase::dismissNotificationByTargetAndType(int targetId, const QString& targetType, const QString& type) {
    if (!m_isOpen) return false;
    const char* sql = "UPDATE notifications SET is_dismissed = 1 WHERE target_id = ? AND target_type = ? AND type = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, targetId);
    sqlite3_bind_text(stmt, 2, targetType.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, type.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

QString BookDatabase::getDatabaseDebugInfo() const {
    if (!m_isOpen) return "Database not open.";

    QString info;
    int userVersion = 0;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            userVersion = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    info += QString("Schema Version: %1\n\n").arg(userVersion);

    info += "Tables:\n";
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), "SELECT name, sql FROM sqlite_master WHERE type='table';",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* namePtr = sqlite3_column_text(stmt, 0);
            QString name = namePtr ? QString::fromUtf8(reinterpret_cast<const char*>(namePtr)) : "";
            info += QString("- %1\n").arg(name);

            const unsigned char* sqlPtr = sqlite3_column_text(stmt, 1);
            if (sqlPtr) {
                info += QString::fromUtf8(reinterpret_cast<const char*>(sqlPtr));
                info += "\n";
            }
        }
        sqlite3_finalize(stmt);
    }

    return info;
}

int BookDatabase::addComment(const QString& entityType, int entityId, const QString& content) {
    if (!m_isOpen) return -1;

    const char* sql = "INSERT INTO comments (entity_type, entity_id, content) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, entityType.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, entityId);
    sqlite3_bind_text(stmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int id = sqlite3_last_insert_rowid(reinterpret_cast<sqlite3*>(m_db));
    sqlite3_finalize(stmt);
    return id;
}

bool BookDatabase::updateComment(int id, const QString& newContent) {
    if (!m_isOpen) return false;

    const char* sql = "UPDATE comments SET content = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, newContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

QList<CommentNode> BookDatabase::getComments(const QString& entityType, int entityId) const {
    QList<CommentNode> items;
    if (!m_isOpen) return items;

    const char* sql =
        "SELECT id, entity_type, entity_id, content, created_at FROM comments WHERE entity_type = ? AND entity_id = ? "
        "ORDER BY created_at ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return items;

    sqlite3_bind_text(stmt, 1, entityType.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, entityId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CommentNode item;
        item.id = sqlite3_column_int(stmt, 0);
        item.entityType = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        item.entityId = sqlite3_column_int(stmt, 2);
        item.content = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        item.timestamp = QDateTime::fromString(
            QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4))), Qt::ISODate);
        items.append(item);
    }

    sqlite3_finalize(stmt);
    return items;
}

bool BookDatabase::deleteComment(int id) {
    if (!m_isOpen) return false;
    const char* sql = "DELETE FROM comments WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

ChatNode BookDatabase::getChat(int messageId) const {
    ChatNode chat;
    chat.id = messageId;
    chat.sendBehavior = "default";
    chat.model = "default";
    chat.multiLine = "default";
    chat.version = 0;

    if (!m_isOpen) return chat;

    const char* sql =
        "SELECT title, systemPrompt, sendBehavior, model, multiLine, draftPrompt, userNote, version FROM chats WHERE "
        "message_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, messageId);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            if (sqlite3_column_text(stmt, 0))
                chat.title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
            if (sqlite3_column_text(stmt, 1))
                chat.systemPrompt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            if (sqlite3_column_text(stmt, 2))
                chat.sendBehavior = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
            if (sqlite3_column_text(stmt, 3))
                chat.model = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
            if (sqlite3_column_text(stmt, 4))
                chat.multiLine = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)));
            if (sqlite3_column_text(stmt, 5))
                chat.draftPrompt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
            if (sqlite3_column_text(stmt, 6))
                chat.userNote = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)));
            chat.version = sqlite3_column_int(stmt, 7);
        }
        sqlite3_finalize(stmt);
    }
    return chat;
}

bool BookDatabase::updateChat(const ChatNode& chat) {
    if (!m_isOpen) return false;

    const char* sql =
        "INSERT OR REPLACE INTO chats (message_id, title, systemPrompt, sendBehavior, model, multiLine, draftPrompt, "
        "userNote, version) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, chat.id);
    sqlite3_bind_text(stmt, 2, chat.title.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, chat.systemPrompt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, chat.sendBehavior.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, chat.model.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, chat.multiLine.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, chat.draftPrompt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, chat.userNote.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, chat.version + 1);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

QSet<int> BookDatabase::getAllChatIds() const {
    QSet<int> ids;
    if (!m_isOpen) return ids;
    const char* sql = "SELECT message_id FROM chats;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ids.insert(sqlite3_column_int(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return ids;
}

bool BookDatabase::deleteTemplate(int id) {
    if (!m_isOpen) return false;
    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, store::ArtifactKind::Template, id);
    if (!artIdOpt.has_value()) return false;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    return store.deleteArtifact(*artIdOpt).isSuccess();
}

bool BookDatabase::updateDocumentTitle(int id, const QString& newTitle, const QString& targetType) {
    if (!m_isOpen) return false;
    auto kindOpt = parseArtifactKind(targetType);
    if (!kindOpt.has_value()) return false;

    auto* rawDb = reinterpret_cast<sqlite3*>(m_db);
    auto artIdOpt = resolveArtifactIdForKind(rawDb, *kindOpt, id);
    if (!artIdOpt.has_value()) return false;

    db::Database database(rawDb);
    store::ArtifactStore store(database);
    auto curVer = store.getCurrentVersion(*artIdOpt);
    if (!curVer.has_value()) return false;

    return updateOrAdvanceArtifact(database, *artIdOpt, newTitle, curVer->content, curVer->metadata);
}

QList<BookDatabase::PromptHistoryEntry> BookDatabase::getPromptHistory(int documentId) const {
    QList<PromptHistoryEntry> items;
    if (!m_isOpen) return items;

    const char* sql =
        "SELECT p.id, p.prompt, p.model, p.timestamp, p.queue_id, q.state "
        "FROM prompt_history p "
        "LEFT JOIN queue q ON p.queue_id = q.id "
        "WHERE p.document_id = ? ORDER BY p.timestamp DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) return items;

    sqlite3_bind_int(stmt, 1, documentId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PromptHistoryEntry e;
        e.id = sqlite3_column_int(stmt, 0);
        e.documentId = documentId;
        e.prompt = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        e.model = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
        e.timestamp = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
        e.queueId = sqlite3_column_int(stmt, 4);

        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
            e.status = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
        } else {
            e.status = "";  // Indicates completed/removed from queue
        }

        items.append(e);
    }
    sqlite3_finalize(stmt);
    return items;
}

QHash<int, QString> BookDatabase::getAllChatTitles() const {
    QHash<int, QString> titles;
    if (!m_isOpen) return titles;
    const char* sql = "SELECT message_id, title FROM chats;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            QString title;
            if (sqlite3_column_text(stmt, 1)) {
                title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
            }
            titles.insert(id, title);
        }
        sqlite3_finalize(stmt);
    }
    return titles;
}
