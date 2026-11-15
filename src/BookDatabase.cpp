#include "BookDatabase.h"

#include <sqlcipher/sqlite3.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

#include "db/Database.h"
#include "db/MigrationFactory.h"
#include "store/ArtifactStore.h"
#include "store/FolderStore.h"

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#endif

namespace {
constexpr int CURRENT_SCHEMA_VERSION = 28;
constexpr int kRestoreJournalVersion = 1;

BookDatabase::RestoreFaultHook g_restoreFaultHook;

bool shouldInjectFault(const QString& stage) { return g_restoreFaultHook && g_restoreFaultHook(stage); }

QString escapeSqlLiteral(const QString& input) {
    QString escaped = input;
    escaped.replace("'", "''");
    return escaped;
}

bool syncParentDirectory(const QString& filePath, QString* error, const QString& faultStage = QString()) {
    if ((!faultStage.isEmpty() && shouldInjectFault(faultStage)) || shouldInjectFault("sync_parent_dir")) {
        if (error) {
            *error = QString("Failed to sync parent directory for %1 (injected fault)").arg(filePath);
        }
        return false;
    }
#ifdef Q_OS_UNIX
    const QString dirPath = QFileInfo(filePath).absolutePath();
    if (dirPath.isEmpty()) {
        if (error) {
            *error = QString("Cannot resolve parent directory for %1").arg(filePath);
        }
        return false;
    }
    const QByteArray dirUtf8 = dirPath.toUtf8();
    const int dirFd = ::open(dirUtf8.constData(), O_RDONLY | O_DIRECTORY);
    if (dirFd < 0) {
        if (error) {
            *error = QString("Failed to open parent directory %1 for sync: %2")
                         .arg(dirPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }
    int syncRc = 0;
    do {
        syncRc = ::fsync(dirFd);
    } while (syncRc != 0 && errno == EINTR);
    if (syncRc != 0) {
        const int savedErrno = errno;
        ::close(dirFd);
        if (error) {
            *error = QString("Failed to fsync parent directory %1: %2")
                         .arg(dirPath, QString::fromLocal8Bit(std::strerror(savedErrno)));
        }
        return false;
    }
    if (::close(dirFd) != 0) {
        if (error) {
            *error = QString("Failed to close parent directory %1 after sync: %2")
                         .arg(dirPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }
    return true;
#else
    Q_UNUSED(filePath);
    return true;
#endif
}

bool removeFileChecked(const QString& path, const QString& faultStage, QString* error) {
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        return true;
    }
    if ((!faultStage.isEmpty() && shouldInjectFault(faultStage)) || !QFile::remove(path)) {
        if (error) {
            *error = QString("Failed to remove %1").arg(path);
        }
        return false;
    }
    return true;
}

bool sameFilesystemTarget(const QString& pathA, const QString& pathB) {
    const QFileInfo infoA(pathA);
    const QFileInfo infoB(pathB);
    if (QDir::cleanPath(infoA.absoluteFilePath()) == QDir::cleanPath(infoB.absoluteFilePath())) {
        return true;
    }
    const QString canonA = infoA.canonicalFilePath();
    const QString canonB = infoB.canonicalFilePath();
    if (!canonA.isEmpty() && !canonB.isEmpty() && canonA == canonB) {
        return true;
    }
    const QString canonDirA = QFileInfo(infoA.absolutePath()).canonicalFilePath();
    const QString canonDirB = QFileInfo(infoB.absolutePath()).canonicalFilePath();
    return !canonDirA.isEmpty() && !canonDirB.isEmpty() && canonDirA == canonDirB &&
           infoA.fileName() == infoB.fileName();
}

bool writeAllFd(int fd, const char* data, qint64 size, QString* error, const char* context) {
    qint64 totalWritten = 0;
    while (totalWritten < size) {
        const ssize_t n = ::write(fd, data + totalWritten, static_cast<size_t>(size - totalWritten));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (error) *error = QString("%1: %2").arg(context, QString::fromLocal8Bit(std::strerror(errno)));
            return false;
        }
        if (n == 0) {
            if (error) *error = QString("%1: unexpected zero-byte write").arg(context);
            return false;
        }
        totalWritten += static_cast<qint64>(n);
    }
    return true;
}

bool writeBytesOwnerOnly(const QString& destPath, const QByteArray& bytes, QString* error) {
#ifdef Q_OS_UNIX
    const QByteArray destUtf8 = destPath.toUtf8();
    const int fd = ::open(destUtf8.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        if (error) {
            *error = QString("Failed to create owner-only file %1: %2")
                         .arg(destPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }
    if (::fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
        if (error) {
            *error = QString("Failed to enforce 0600 permissions on %1: %2")
                         .arg(destPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        ::close(fd);
        QFile::remove(destPath);
        return false;
    }
    if (!writeAllFd(fd, bytes.constData(), bytes.size(), error, "Failed writing file")) {
        ::close(fd);
        QFile::remove(destPath);
        return false;
    }
    if (::fsync(fd) != 0) {
        if (error) {
            *error = QString("Failed to fsync file %1: %2").arg(destPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        ::close(fd);
        QFile::remove(destPath);
        return false;
    }
    if (::close(fd) != 0) {
        if (error) {
            *error = QString("Failed to close file %1: %2").arg(destPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        QFile::remove(destPath);
        return false;
    }
    return true;
#else
    QFile dst(destPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error) *error = QString("Failed to create destination file: %1").arg(dst.errorString());
        return false;
    }
    if (!dst.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        dst.close();
        QFile::remove(destPath);
        if (error) *error = "Failed to restrict destination file permissions";
        return false;
    }
    qint64 totalWritten = 0;
    while (totalWritten < bytes.size()) {
        const qint64 n = dst.write(bytes.constData() + totalWritten, bytes.size() - totalWritten);
        if (n <= 0) {
            dst.close();
            QFile::remove(destPath);
            if (error) *error = QString("Failed to write destination file: %1").arg(dst.errorString());
            return false;
        }
        totalWritten += n;
    }
    if (!dst.flush()) {
        dst.close();
        QFile::remove(destPath);
        if (error) *error = QString("Failed to flush destination file: %1").arg(dst.errorString());
        return false;
    }
    dst.close();
    return true;
#endif
}

bool copyFileOwnerOnly(const QString& sourcePath, const QString& destPath, QString* error) {
    QFile src(sourcePath);
    if (!src.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("Failed to open source file for copy: %1").arg(src.errorString());
        return false;
    }

#ifdef Q_OS_UNIX
    const QByteArray destUtf8 = destPath.toUtf8();
    const int dstFd = ::open(destUtf8.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (dstFd < 0) {
        if (error) {
            *error = QString("Failed to create destination file %1: %2")
                         .arg(destPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }
    if (::fchmod(dstFd, S_IRUSR | S_IWUSR) != 0) {
        if (error) {
            *error = QString("Failed to enforce 0600 permissions on %1: %2")
                         .arg(destPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        ::close(dstFd);
        QFile::remove(destPath);
        return false;
    }

    constexpr qint64 kChunkSize = 65536;
    QByteArray buffer(kChunkSize, Qt::Uninitialized);
    while (!src.atEnd()) {
        const qint64 bytesRead = src.read(buffer.data(), kChunkSize);
        if (bytesRead < 0) {
            if (error) *error = QString("Failed to read source file during copy: %1").arg(src.errorString());
            ::close(dstFd);
            QFile::remove(destPath);
            return false;
        }
        if (bytesRead == 0) continue;
        if (!writeAllFd(dstFd, buffer.constData(), bytesRead, error, "Failed to write destination file during copy")) {
            ::close(dstFd);
            QFile::remove(destPath);
            return false;
        }
    }

    if (::fsync(dstFd) != 0) {
        if (error) {
            *error = QString("Failed to fsync destination file %1: %2")
                         .arg(destPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        ::close(dstFd);
        QFile::remove(destPath);
        return false;
    }
    if (::close(dstFd) != 0) {
        if (error) {
            *error = QString("Failed to close destination file %1: %2")
                         .arg(destPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        QFile::remove(destPath);
        return false;
    }
    return true;
#else
    QFile dst(destPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error) *error = QString("Failed to create destination file: %1").arg(dst.errorString());
        return false;
    }
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
#endif
}

bool atomicReplaceFile(const QString& srcPath, const QString& dstPath, QString* error,
                       const QString& syncFaultStage = QString()) {
#ifdef Q_OS_UNIX
    const QByteArray srcUtf8 = srcPath.toUtf8();
    const QByteArray dstUtf8 = dstPath.toUtf8();
    if (::rename(srcUtf8.constData(), dstUtf8.constData()) != 0) {
        if (error) {
            *error = QString("Failed atomic rename %1 -> %2: %3")
                         .arg(srcPath, dstPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }
    return syncParentDirectory(dstPath, error, syncFaultStage);
#else
    const QString prevPath = QString("%1.%2.prev").arg(dstPath, QUuid::createUuid().toString(QUuid::Id128).toLower());
    const bool hadDst = QFile::exists(dstPath);
    if (hadDst && !QFile::rename(dstPath, prevPath)) {
        if (error) *error = QString("Failed to stage existing file %1 for replacement").arg(dstPath);
        return false;
    }
    if (!QFile::rename(srcPath, dstPath)) {
        if (hadDst) {
            QFile::rename(prevPath, dstPath);
        }
        if (error) *error = QString("Failed to move %1 into place at %2").arg(srcPath, dstPath);
        return false;
    }
    if (!syncParentDirectory(dstPath, error, syncFaultStage)) {
        return false;
    }
    if (hadDst && !QFile::remove(prevPath)) {
        if (error) *error = QString("Failed to remove temporary previous file %1").arg(prevPath);
        return false;
    }
    return syncParentDirectory(dstPath, error, syncFaultStage);
#endif
}

class ExclusiveLockGuard {
   public:
    explicit ExclusiveLockGuard(sqlite3* handle) : m_handle(handle), m_active(handle != nullptr) {}
    ~ExclusiveLockGuard() { release(); }
    ExclusiveLockGuard(const ExclusiveLockGuard&) = delete;
    ExclusiveLockGuard& operator=(const ExclusiveLockGuard&) = delete;

    void release() {
        if (!m_active || !m_handle) {
            return;
        }
        m_active = false;
        char* errMsg = nullptr;
        const int rc = sqlite3_exec(m_handle, "ROLLBACK;", nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            qWarning() << "Failed to release backup snapshot exclusive lock:"
                       << (errMsg ? QString::fromUtf8(errMsg) : QString::fromUtf8(sqlite3_errmsg(m_handle)));
            sqlite3_free(errMsg);
        }
    }

   private:
    sqlite3* m_handle = nullptr;
    bool m_active = false;
};

struct FileFingerprint {
    bool exists = false;
    qint64 size = -1;
#ifdef Q_OS_UNIX
    dev_t dev = 0;
    ino_t ino = 0;
    qint64 mtimeSec = 0;
    qint64 mtimeNsec = 0;
#else
    qint64 mtimeMs = 0;
#endif
    bool operator==(const FileFingerprint& other) const {
        if (exists != other.exists || size != other.size) {
            return false;
        }
#ifdef Q_OS_UNIX
        return dev == other.dev && ino == other.ino && mtimeSec == other.mtimeSec && mtimeNsec == other.mtimeNsec;
#else
        return mtimeMs == other.mtimeMs;
#endif
    }
    bool operator!=(const FileFingerprint& other) const { return !(*this == other); }
};

FileFingerprint captureFileFingerprint(const QString& path) {
    FileFingerprint fp;
#ifdef Q_OS_UNIX
    struct stat st{};
    const QByteArray utf8 = path.toUtf8();
    if (::stat(utf8.constData(), &st) == 0) {
        fp.exists = true;
        fp.size = static_cast<qint64>(st.st_size);
        fp.dev = st.st_dev;
        fp.ino = st.st_ino;
        fp.mtimeSec = static_cast<qint64>(st.st_mtim.tv_sec);
        fp.mtimeNsec = static_cast<qint64>(st.st_mtim.tv_nsec);
    }
#else
    const QFileInfo info(path);
    if (info.exists()) {
        fp.exists = true;
        fp.size = info.size();
        fp.mtimeMs = info.lastModified().toMSecsSinceEpoch();
    }
#endif
    return fp;
}

bool checkpointWalStrict(sqlite3* handle, const char* context, QString* error) {
    if (!handle) {
        if (error) *error = QString("Database handle is null before %1 WAL checkpoint").arg(context);
        return false;
    }
    int logFrames = 0;
    int checkpointedFrames = 0;
    const int walRc =
        sqlite3_wal_checkpoint_v2(handle, nullptr, SQLITE_CHECKPOINT_TRUNCATE, &logFrames, &checkpointedFrames);
    if (walRc != SQLITE_OK || (logFrames > 0 && checkpointedFrames < logFrames)) {
        if (error) {
            *error = QString("WAL checkpoint incomplete or busy before %1 (rc=%2, log=%3, checkpointed=%4): %5")
                         .arg(context)
                         .arg(walRc)
                         .arg(logFrames)
                         .arg(checkpointedFrames)
                         .arg(QString::fromUtf8(sqlite3_errmsg(handle)));
        }
        return false;
    }
    return true;
}

bool verifyEncryptedFileIntegrity(const QString& path, const QString& password, QString* error,
                                  bool cleanTransientWal = false) {
    QFile file(path);
    if (!file.exists()) {
        if (error) *error = QString("Database file does not exist: %1").arg(path);
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("Failed to open database file for verification: %1").arg(path);
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

    const bool hadWalBefore = QFile::exists(path + "-wal");
    const bool hadShmBefore = QFile::exists(path + "-shm");

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
    const int stepRc = sqlite3_step(stmt);
    if (stepRc == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) integrityResult = QString::fromUtf8(reinterpret_cast<const char*>(text));
    }
    const int doneRc = (stepRc == SQLITE_ROW) ? sqlite3_step(stmt) : stepRc;
    sqlite3_finalize(stmt);
    sqlite3_close(checkDb);

    if (cleanTransientWal) {
        const QString walPath = path + "-wal";
        const QString shmPath = path + "-shm";
        if (!hadWalBefore && QFile::exists(walPath) && QFileInfo(walPath).size() == 0) {
            QFile::remove(walPath);
        }
        if (!hadShmBefore && QFile::exists(shmPath)) {
            QFile::remove(shmPath);
        }
    }

    if (stepRc != SQLITE_ROW || doneRc != SQLITE_DONE || integrityResult != "ok") {
        if (error) {
            *error = QString("Database integrity check failed: %1")
                         .arg(integrityResult.isEmpty() ? QString("unreadable") : integrityResult);
        }
        return false;
    }
    return true;
}

struct RestoreJournal {
    int version = kRestoreJournalVersion;
    QString opId;
    QString phase;
    QString targetPath;
    QString stagedNewPath;
    QString rollbackDbPath;
    QString rollbackWalPath;
    QString rollbackShmPath;
    bool hadOriginalDb = false;
    bool hadOriginalWal = false;
    bool hadOriginalShm = false;
};

struct RestorePaths {
    QString journalPath;
    QString stagedNewPath;
    QString rollbackDbPath;
    QString rollbackWalPath;
    QString rollbackShmPath;
    QString abortedDbPath;
    QString abortedWalPath;
    QString abortedShmPath;
};

QString restoreJournalPathFor(const QString& dbPath) { return dbPath + ".restore_journal.json"; }

bool isValidOperationId(const QString& opId) {
    if (opId.size() != 32) {
        return false;
    }
    return std::all_of(opId.cbegin(), opId.cend(), [](const QChar ch) {
        const ushort code = ch.unicode();
        return (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f');
    });
}

RestorePaths deriveRestorePaths(const QString& dbPath, const QString& opId) {
    RestorePaths paths;
    paths.journalPath = restoreJournalPathFor(dbPath);
    paths.stagedNewPath = QString("%1.restore.%2.new").arg(dbPath, opId);
    paths.rollbackDbPath = QString("%1.restore.%2.orig").arg(dbPath, opId);
    paths.rollbackWalPath = paths.rollbackDbPath + "-wal";
    paths.rollbackShmPath = paths.rollbackDbPath + "-shm";
    paths.abortedDbPath = QString("%1.restore.%2.aborted").arg(dbPath, opId);
    paths.abortedWalPath = paths.abortedDbPath + "-wal";
    paths.abortedShmPath = paths.abortedDbPath + "-shm";
    return paths;
}

bool isValidRestorePhase(const QString& phase) {
    return phase == "staging" || phase == "moving_original" || phase == "placing_replacement" || phase == "verifying" ||
           phase == "rolling_back";
}

bool writeRestoreJournal(const RestoreJournal& journal, QString* error) {
    if (!isValidOperationId(journal.opId) || !isValidRestorePhase(journal.phase) || journal.targetPath.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Cannot write restore journal with invalid metadata");
        }
        return false;
    }
    const QString journalPath = restoreJournalPathFor(journal.targetPath);
    if (QFileInfo(journalPath).isSymLink()) {
        if (error) {
            *error = QString("Refusing to overwrite symlinked restore journal %1").arg(journalPath);
        }
        return false;
    }

    QJsonObject obj;
    obj["version"] = journal.version;
    obj["op_id"] = journal.opId;
    obj["phase"] = journal.phase;
    obj["target_path"] = journal.targetPath;
    obj["staged_new_path"] = journal.stagedNewPath;
    obj["rollback_db_path"] = journal.rollbackDbPath;
    obj["rollback_wal_path"] = journal.rollbackWalPath;
    obj["rollback_shm_path"] = journal.rollbackShmPath;
    obj["had_original_db"] = journal.hadOriginalDb;
    obj["had_original_wal"] = journal.hadOriginalWal;
    obj["had_original_shm"] = journal.hadOriginalShm;

    const QString tmpPath = QString("%1.%2.tmp").arg(journalPath, QUuid::createUuid().toString(QUuid::Id128).toLower());
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    if (!writeBytesOwnerOnly(tmpPath, bytes, error)) {
        return false;
    }
    if (!atomicReplaceFile(tmpPath, journalPath, error, "sync_restore_journal")) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

bool readRestoreJournal(const QString& dbPath, RestoreJournal* out, QString* error) {
    if (dbPath.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Invalid empty database path for restore journal");
        }
        return false;
    }

    const QString walPath = dbPath + "-wal";
    const QString shmPath = dbPath + "-shm";
    const QString journalPath = restoreJournalPathFor(dbPath);
    const QFileInfo dbInfo(dbPath);
    const QFileInfo walInfo(walPath);
    const QFileInfo shmInfo(shmPath);
    const QFileInfo journalInfo(journalPath);
    if (dbInfo.isSymLink() || walInfo.isSymLink() || shmInfo.isSymLink() || journalInfo.isSymLink()) {
        if (error) {
            *error =
                QString("Refusing restore recovery with symlinked target or journal path under %1").arg(journalPath);
        }
        return false;
    }
    if (!journalInfo.exists() || !journalInfo.isFile() || journalInfo.size() > 65536) {
        if (error) {
            *error = QString("Invalid or unreadable restore journal %1; refusing recovery").arg(journalPath);
        }
        return false;
    }

    QFile file(journalPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QString("Failed to read restore journal %1: %2").arg(journalPath, file.errorString());
        }
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) {
            *error = QString("Corrupt restore journal %1 (%2); manual inspection required before touching book files")
                         .arg(journalPath, parseError.errorString());
        }
        return false;
    }

    const QJsonObject obj = doc.object();
    const QJsonValue versionVal = obj.value("version");
    const QJsonValue opIdVal = obj.value("op_id");
    const QJsonValue phaseVal = obj.value("phase");
    const QJsonValue targetVal = obj.value("target_path");
    const QJsonValue hadDbVal = obj.value("had_original_db");
    const QJsonValue hadWalVal = obj.value("had_original_wal");
    const QJsonValue hadShmVal = obj.value("had_original_shm");
    if (!versionVal.isDouble() || versionVal.toDouble() != static_cast<double>(kRestoreJournalVersion) ||
        !opIdVal.isString() || !isValidOperationId(opIdVal.toString()) || !phaseVal.isString() ||
        !isValidRestorePhase(phaseVal.toString()) || !targetVal.isString() || targetVal.toString() != dbPath ||
        !hadDbVal.isBool() || !hadWalVal.isBool() || !hadShmVal.isBool()) {
        if (error) {
            *error = QString("Invalid or ambiguous restore journal %1; refusing destructive cleanup").arg(journalPath);
        }
        return false;
    }

    const bool hadOriginalDb = hadDbVal.toBool();
    const bool hadOriginalWal = hadWalVal.toBool();
    const bool hadOriginalShm = hadShmVal.toBool();
    if (!hadOriginalDb && (hadOriginalWal || hadOriginalShm)) {
        if (error) {
            *error =
                QString("Inconsistent restore journal %1: sidecar flag set without original database").arg(journalPath);
        }
        return false;
    }

    const QString opId = opIdVal.toString();
    const RestorePaths expected = deriveRestorePaths(dbPath, opId);
    const QJsonValue stagedVal = obj.value("staged_new_path");
    const QJsonValue rollbackDbVal = obj.value("rollback_db_path");
    const QJsonValue rollbackWalVal = obj.value("rollback_wal_path");
    const QJsonValue rollbackShmVal = obj.value("rollback_shm_path");
    if (!stagedVal.isString() || stagedVal.toString() != expected.stagedNewPath || !rollbackDbVal.isString() ||
        rollbackDbVal.toString() != expected.rollbackDbPath || !rollbackWalVal.isString() ||
        rollbackWalVal.toString() != expected.rollbackWalPath || !rollbackShmVal.isString() ||
        rollbackShmVal.toString() != expected.rollbackShmPath) {
        if (error) {
            *error = QString("Restore journal %1 contains unexpected or mismatched recovery paths; refusing recovery")
                         .arg(journalPath);
        }
        return false;
    }

    const QStringList derivedList = {expected.stagedNewPath,   expected.rollbackDbPath, expected.rollbackWalPath,
                                     expected.rollbackShmPath, expected.abortedDbPath,  expected.abortedWalPath,
                                     expected.abortedShmPath};
    for (const QString& derivedPath : derivedList) {
        const QFileInfo info(derivedPath);
        if (info.isSymLink() || (info.exists() && !info.isFile())) {
            if (error) {
                *error = QString("Restore recovery artifact %1 is a symlink or non-regular file; refusing recovery")
                             .arg(derivedPath);
            }
            return false;
        }
    }

    if (out) {
        out->version = kRestoreJournalVersion;
        out->opId = opId;
        out->phase = phaseVal.toString();
        out->targetPath = dbPath;
        out->stagedNewPath = expected.stagedNewPath;
        out->rollbackDbPath = expected.rollbackDbPath;
        out->rollbackWalPath = expected.rollbackWalPath;
        out->rollbackShmPath = expected.rollbackShmPath;
        out->hadOriginalDb = hadOriginalDb;
        out->hadOriginalWal = hadOriginalWal;
        out->hadOriginalShm = hadOriginalShm;
    }
    return true;
}

bool enforceOwnerOnlyIfExists(const QString& path, const char* label, QString* error) {
    if (!QFile::exists(path)) {
        return true;
    }
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (error) {
            *error = QString("Failed to enforce owner-only permissions on %1 %2").arg(label, path);
        }
        return false;
    }
    return true;
}

bool verifyRecoveredDatabaseFile(const QString& dbPath, const QString& passwordToVerify, const QString& journalPath,
                                 QString* error) {
    if (!passwordToVerify.isEmpty()) {
        return verifyEncryptedFileIntegrity(dbPath, passwordToVerify, error);
    }
    if (QFileInfo(dbPath).size() <= 0) {
        if (error) {
            *error = QString("Recovered database %1 is empty; retaining journal %2").arg(dbPath, journalPath);
        }
        return false;
    }
    return true;
}

bool commitAndCleanRecoveryArtifacts(const QString& dbPath, const QString& journalPath,
                                     const QList<std::pair<QString, QString>>& cleanupTargets, QString* error) {
    if (!removeFileChecked(journalPath, "recovery_remove_journal", error)) {
        return false;
    }
    if (!syncParentDirectory(journalPath, error, "sync_recovery_after_journal_remove")) {
        return false;
    }

    QStringList cleanupFailures;
    for (const auto& entry : cleanupTargets) {
        QString rmErr;
        if (!removeFileChecked(entry.first, entry.second, &rmErr)) {
            cleanupFailures.append(rmErr);
        }
    }
    QString finalSyncErr;
    if (!syncParentDirectory(dbPath, &finalSyncErr, "sync_recovery_cleanup")) {
        cleanupFailures.append(finalSyncErr);
    }
    if (!cleanupFailures.isEmpty()) {
        if (error) {
            *error = QString("Recovered database, but post-recovery cleanup or sync failed: %1")
                         .arg(cleanupFailures.join("; "));
        }
        return false;
    }
    return true;
}

bool recoverStagingPhase(const QString& dbPath, const RestoreJournal& journal, const QString& journalPath,
                         QString* error) {
    const bool originalMismatch = (journal.hadOriginalDb != QFile::exists(dbPath));
    if (originalMismatch || QFile::exists(journal.rollbackDbPath) || QFile::exists(journal.rollbackWalPath) ||
        QFile::exists(journal.rollbackShmPath)) {
        if (error) {
            *error = QString(
                         "Ambiguous staging-phase restore state for %1 (journal %2); "
                         "refusing automatic cleanup")
                         .arg(dbPath, journalPath);
        }
        return false;
    }
    if (!removeFileChecked(journal.stagedNewPath, "recovery_remove_staged", error)) {
        return false;
    }
    if (!syncParentDirectory(dbPath, error, "sync_recovery_staging_staged")) {
        return false;
    }
    if (!removeFileChecked(journalPath, "recovery_remove_journal", error)) {
        return false;
    }
    return syncParentDirectory(journalPath, error, "sync_recovery_staging");
}

bool recoverMovingOriginalPhase(const QString& dbPath, const RestoreJournal& journal, const QString& journalPath,
                                const QString& passwordToVerify, QString* error) {
    const QString walPath = dbPath + "-wal";
    const QString shmPath = dbPath + "-shm";

    if (!journal.hadOriginalDb && (QFile::exists(journal.rollbackDbPath) || QFile::exists(journal.rollbackWalPath) ||
                                   QFile::exists(journal.rollbackShmPath))) {
        if (error) {
            *error = QString("Ambiguous restore journal %1: unexpected rollback files without original database")
                         .arg(journalPath);
        }
        return false;
    }

    if ((QFile::exists(dbPath) && QFile::exists(journal.rollbackDbPath)) ||
        (QFile::exists(walPath) && QFile::exists(journal.rollbackWalPath)) ||
        (QFile::exists(shmPath) && QFile::exists(journal.rollbackShmPath))) {
        if (error) {
            *error = QString(
                         "Ambiguous interrupted restore state (both live and rollback files exist under %1); "
                         "manual recovery required")
                         .arg(journalPath);
        }
        return false;
    }

    if (!QFile::exists(dbPath) && QFile::exists(journal.rollbackDbPath) &&
        !QFile::rename(journal.rollbackDbPath, dbPath)) {
        if (error) {
            *error = QString("Failed to move rollback database %1 back to %2").arg(journal.rollbackDbPath, dbPath);
        }
        return false;
    }
    if (!QFile::exists(walPath) && QFile::exists(journal.rollbackWalPath) &&
        !QFile::rename(journal.rollbackWalPath, walPath)) {
        if (error) {
            *error = QString("Failed to move rollback WAL sidecar %1 back to %2").arg(journal.rollbackWalPath, walPath);
        }
        return false;
    }
    if (!QFile::exists(shmPath) && QFile::exists(journal.rollbackShmPath) &&
        !QFile::rename(journal.rollbackShmPath, shmPath)) {
        if (error) {
            *error = QString("Failed to move rollback SHM sidecar %1 back to %2").arg(journal.rollbackShmPath, shmPath);
        }
        return false;
    }

    if (journal.hadOriginalDb && !QFile::exists(dbPath)) {
        if (error) {
            *error = QString("Original database missing and no surviving rollback copy found at %1 (journal %2)")
                         .arg(journal.rollbackDbPath, journalPath);
        }
        return false;
    }

    if (journal.hadOriginalDb) {
        if (!enforceOwnerOnlyIfExists(dbPath, "recovered database", error) ||
            !enforceOwnerOnlyIfExists(walPath, "recovered WAL", error) ||
            !enforceOwnerOnlyIfExists(shmPath, "recovered SHM", error)) {
            return false;
        }
    }

    if (!syncParentDirectory(dbPath, error, "sync_recovery_restore_original")) {
        return false;
    }
    if (journal.hadOriginalDb && !verifyRecoveredDatabaseFile(dbPath, passwordToVerify, journalPath, error)) {
        return false;
    }

    return commitAndCleanRecoveryArtifacts(dbPath, journalPath,
                                           {{journal.stagedNewPath, QStringLiteral("recovery_remove_staged")}}, error);
}

bool recoverRollbackWithOriginal(const QString& dbPath, const RestoreJournal& journal, const QString& journalPath,
                                 const QString& passwordToVerify, QString* error) {
    const QString walPath = dbPath + "-wal";
    const QString shmPath = dbPath + "-shm";
    const RestorePaths paths = deriveRestorePaths(dbPath, journal.opId);

    if (!QFile::exists(journal.rollbackDbPath)) {
        if (journal.phase != "rolling_back" || !QFile::exists(dbPath)) {
            if (error) {
                *error = QString(
                             "Restore journal %1 requires rollback copy %2 which is missing; "
                             "refusing destructive recovery")
                             .arg(journalPath, journal.rollbackDbPath);
            }
            return false;
        }
        if ((QFile::exists(walPath) && QFile::exists(journal.rollbackWalPath)) ||
            (QFile::exists(shmPath) && QFile::exists(journal.rollbackShmPath))) {
            if (error) {
                *error = QString("Ambiguous rollback state (both live and rollback sidecars exist under %1)")
                             .arg(journalPath);
            }
            return false;
        }
        if (QFile::exists(journal.rollbackWalPath) && !QFile::rename(journal.rollbackWalPath, walPath)) {
            if (error) {
                *error = QString("Failed to restore rollback WAL %1 -> %2").arg(journal.rollbackWalPath, walPath);
            }
            return false;
        }
        if (QFile::exists(journal.rollbackShmPath) && !QFile::rename(journal.rollbackShmPath, shmPath)) {
            if (error) {
                *error = QString("Failed to restore rollback SHM %1 -> %2").arg(journal.rollbackShmPath, shmPath);
            }
            return false;
        }
    } else {
        if ((QFile::exists(dbPath) && QFile::exists(paths.abortedDbPath)) ||
            (QFile::exists(walPath) && QFile::exists(paths.abortedWalPath)) ||
            (QFile::exists(shmPath) && QFile::exists(paths.abortedShmPath))) {
            if (error) {
                *error =
                    QString("Ambiguous rollback state: aborted target files already exist for %1").arg(journalPath);
            }
            return false;
        }

        RestoreJournal rollingJournal = journal;
        rollingJournal.phase = QStringLiteral("rolling_back");
        if (!writeRestoreJournal(rollingJournal, error)) {
            return false;
        }

        if (QFile::exists(dbPath) && !QFile::rename(dbPath, paths.abortedDbPath)) {
            if (error) {
                *error = QString("Failed to move unverified replacement %1 aside to %2; rollback copy %3 preserved")
                             .arg(dbPath, paths.abortedDbPath, journal.rollbackDbPath);
            }
            return false;
        }
        if (QFile::exists(walPath) && !QFile::rename(walPath, paths.abortedWalPath)) {
            if (error) {
                *error = QString("Failed to move unverified WAL %1 aside; rollback copy %2 preserved")
                             .arg(walPath, journal.rollbackDbPath);
            }
            return false;
        }
        if (QFile::exists(shmPath) && !QFile::rename(shmPath, paths.abortedShmPath)) {
            if (error) {
                *error = QString("Failed to move unverified SHM %1 aside; rollback copy %2 preserved")
                             .arg(shmPath, journal.rollbackDbPath);
            }
            return false;
        }
        if (!syncParentDirectory(dbPath, error, "sync_recovery_move_aborted")) {
            return false;
        }

        if (QFile::exists(dbPath) || QFile::exists(walPath) || QFile::exists(shmPath)) {
            if (error) {
                *error = QString("Cannot restore %1 while target or sidecar path is still occupied").arg(dbPath);
            }
            return false;
        }
        if (!QFile::rename(journal.rollbackDbPath, dbPath)) {
            if (error) {
                *error = QString("Failed to restore rollback database %1 -> %2").arg(journal.rollbackDbPath, dbPath);
            }
            return false;
        }
        if (shouldInjectFault("crash_recovery_after_rollback_db")) {
            if (error) {
                *error = QStringLiteral("Simulated crash during recovery after restoring rollback database");
            }
            return false;
        }
        if (QFile::exists(journal.rollbackWalPath) && !QFile::rename(journal.rollbackWalPath, walPath)) {
            if (error) {
                *error = QString("Failed to restore rollback WAL %1 -> %2").arg(journal.rollbackWalPath, walPath);
            }
            return false;
        }
        if (QFile::exists(journal.rollbackShmPath) && !QFile::rename(journal.rollbackShmPath, shmPath)) {
            if (error) {
                *error = QString("Failed to restore rollback SHM %1 -> %2").arg(journal.rollbackShmPath, shmPath);
            }
            return false;
        }
    }

    if (!enforceOwnerOnlyIfExists(dbPath, "recovered database", error) ||
        !enforceOwnerOnlyIfExists(walPath, "recovered WAL", error) ||
        !enforceOwnerOnlyIfExists(shmPath, "recovered SHM", error)) {
        return false;
    }
    if (!syncParentDirectory(dbPath, error, "sync_recovery_restore_original")) {
        return false;
    }
    if (!verifyRecoveredDatabaseFile(dbPath, passwordToVerify, journalPath, error)) {
        return false;
    }

    return commitAndCleanRecoveryArtifacts(dbPath, journalPath,
                                           {{paths.abortedDbPath, QStringLiteral("recovery_remove_aborted")},
                                            {paths.abortedWalPath, QStringLiteral("recovery_remove_aborted")},
                                            {paths.abortedShmPath, QStringLiteral("recovery_remove_aborted")},
                                            {journal.stagedNewPath, QStringLiteral("recovery_remove_staged")}},
                                           error);
}

bool recoverNoOriginalTarget(const QString& dbPath, const RestoreJournal& journal, const QString& journalPath,
                             QString* error) {
    if (QFile::exists(journal.rollbackDbPath) || QFile::exists(journal.rollbackWalPath) ||
        QFile::exists(journal.rollbackShmPath)) {
        if (error) {
            *error = QString("Ambiguous restore journal %1: rollback copy exists when had_original_db is false")
                         .arg(journalPath);
        }
        return false;
    }

    const QString walPath = dbPath + "-wal";
    const QString shmPath = dbPath + "-shm";
    if (!removeFileChecked(dbPath, "recovery_remove_unverified_db", error) ||
        !removeFileChecked(walPath, "recovery_remove_unverified_wal", error) ||
        !removeFileChecked(shmPath, "recovery_remove_unverified_shm", error)) {
        return false;
    }
    if (!syncParentDirectory(dbPath, error, "sync_recovery_no_original_cleanup")) {
        return false;
    }

    return commitAndCleanRecoveryArtifacts(dbPath, journalPath,
                                           {{journal.stagedNewPath, QStringLiteral("recovery_remove_staged")}}, error);
}

QStringList findUnjournaledRollbackFiles(const QString& dbPath) {
    QStringList found;
    const QString legacyPreRestore = dbPath + ".pre_restore.bak";
    if (QFile::exists(legacyPreRestore)) {
        found.append(legacyPreRestore);
    }
    const QFileInfo dbInfo(dbPath);
    const QDir dir(dbInfo.absolutePath());
    if (dir.exists()) {
        const QStringList entries =
            dir.entryList({dbInfo.fileName() + ".restore.*.orig"}, QDir::Files | QDir::Hidden | QDir::System);
        for (const QString& entry : entries) {
            found.append(dir.filePath(entry));
        }
    }
    return found;
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

void BookDatabase::setRestoreFaultHookForTesting(RestoreFaultHook hook) { g_restoreFaultHook = std::move(hook); }

bool BookDatabase::openExactSchema(const QString& password, QString* error) {
    if (m_isOpen) return true;

    bool createdNewFile = false;
    if (!m_filepath.isEmpty() && m_filepath != ":memory:" && !QFile::exists(m_filepath)) {
#ifdef Q_OS_UNIX
        const QByteArray dbUtf8 = m_filepath.toUtf8();
        const int newFd = ::open(dbUtf8.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR);
        if (newFd >= 0) {
            ::fchmod(newFd, S_IRUSR | S_IWUSR);
            ::close(newFd);
            createdNewFile = true;
        }
#endif
    }
    auto removeIfCreatedNew = [&]() {
        if (createdNewFile) {
            QFile::remove(m_filepath);
        }
    };

    int rc = sqlite3_open(m_filepath.toUtf8().constData(), reinterpret_cast<sqlite3**>(&m_db));
    if (rc != SQLITE_OK) {
        const QString msg = QString("Cannot open database: %1")
                                .arg(m_db ? QString::fromUtf8(sqlite3_errmsg(reinterpret_cast<sqlite3*>(m_db)))
                                          : QStringLiteral("sqlite3_open failed"));
        qWarning() << msg;
        if (error) *error = msg;
        if (m_db) {
            sqlite3_close(reinterpret_cast<sqlite3*>(m_db));
            m_db = nullptr;
        }
        removeIfCreatedNew();
        return false;
    }

    if (!password.isEmpty()) {
        QString keyPragma = QString("PRAGMA key = '%1';").arg(escapeSqlLiteral(password));
        char* errMsg = nullptr;
        rc = sqlite3_exec(reinterpret_cast<sqlite3*>(m_db), keyPragma.toUtf8().constData(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            const QString msg = QString("Failed to set key: %1").arg(errMsg ? QString::fromUtf8(errMsg) : QString());
            qWarning() << msg;
            if (error) *error = msg;
            sqlite3_free(errMsg);
            close();
            removeIfCreatedNew();
            return false;
        }
    }

    // Verify key works
    char* errMsg = nullptr;
    rc = sqlite3_exec(reinterpret_cast<sqlite3*>(m_db), "SELECT count(*) FROM sqlite_master;", nullptr, nullptr,
                      &errMsg);
    if (rc != SQLITE_OK) {
        const QString msg =
            QString("Invalid password or corrupted database: %1").arg(errMsg ? QString::fromUtf8(errMsg) : QString());
        qWarning() << msg;
        if (error) *error = msg;
        sqlite3_free(errMsg);
        close();
        removeIfCreatedNew();
        return false;
    }

    db::Database database(reinterpret_cast<sqlite3*>(m_db));
    QString databaseError;
    if (!database.execute("PRAGMA foreign_keys = ON;", &databaseError)) {
        const QString msg = QString("Failed to enable foreign keys: %1").arg(databaseError);
        qWarning() << msg;
        if (error) *error = msg;
        close();
        removeIfCreatedNew();
        return false;
    }

    int foreignKeysEnabled = 0;
    if (!database.queryInt("PRAGMA foreign_keys;", foreignKeysEnabled, &databaseError) || foreignKeysEnabled != 1) {
        const QString msg = QStringLiteral("Failed to verify foreign keys are enabled");
        qWarning() << msg;
        if (error) *error = msg;
        close();
        removeIfCreatedNew();
        return false;
    }

    if (!m_filepath.isEmpty() && m_filepath != ":memory:") {
        QFile::setPermissions(m_filepath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    m_password = password;
    m_isOpen = true;
    return true;
}

bool BookDatabase::openInternal(const QString& password, bool runStartupRecovery, QString* error) {
    if (m_isOpen) return true;
    m_lastError.clear();

    if (runStartupRecovery && !m_filepath.isEmpty() && m_filepath != ":memory:") {
        const QString journalPath = restoreJournalPathFor(m_filepath);
        const QFileInfo journalInfo(journalPath);
        if (journalInfo.exists() || journalInfo.isSymLink()) {
            QString recoveryError;
            if (!recoverInterruptedRestoreInternal(password, &recoveryError)) {
                m_lastError = recoveryError;
                if (error) *error = recoveryError;
                qWarning() << "Interrupted restore recovery failed on open:" << recoveryError;
                return false;
            }
        }
        if (!QFile::exists(m_filepath)) {
            const QStringList orphanedRollbackFiles = findUnjournaledRollbackFiles(m_filepath);
            if (!orphanedRollbackFiles.isEmpty()) {
                const QString msg = QString(
                                        "Book database file is missing while recovery rollback files exist (%1); "
                                        "refusing to initialize an empty book over surviving recovery state")
                                        .arg(orphanedRollbackFiles.join(", "));
                m_lastError = msg;
                if (error) *error = msg;
                qWarning() << msg;
                return false;
            }
        }
    }

    QString openErr;
    if (!openExactSchema(password, &openErr)) {
        m_lastError = openErr;
        if (error) *error = openErr;
        return false;
    }

    if (!initSchema()) {
        if (m_lastError.isEmpty()) {
            m_lastError = QStringLiteral("Database schema initialization/migration failed");
        }
        if (error) *error = m_lastError;
        close();
        return false;
    }

    cleanupDeadProcessingItems();
    return true;
}

bool BookDatabase::open(const QString& password) { return openInternal(password, true, nullptr); }

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
            QString itemError = errText ? QString::fromUtf8(reinterpret_cast<const char*>(errText)) : QString();

            if (pid <= 0) {
                itemsToReset.append({id, itemError});
            } else {
#ifdef Q_OS_UNIX
                if (kill(pid, 0) != 0 && errno == ESRCH) {
                    itemsToReset.append({id, itemError});
                } else {
                    // Process exists, check if it's kllamabooks
                    QFile commFile(QString("/proc/%1/comm").arg(pid));
                    if (commFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        QString comm = QString(commFile.readAll()).trimmed();
                        if (!comm.contains("kllamabooks", Qt::CaseInsensitive)) {
                            itemsToReset.append({id, itemError});
                        }
                    } else {
                        // Could not read comm file, assume dead
                        itemsToReset.append({id, itemError});
                    }
                }
#else
                // Simple fallback for non-Unix if needed, though KllamaBooks is KDE/Linux targeted.
                itemsToReset.append({id, itemError});
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
    const QFileInfo backupInfo(backupPath);
    if (backupPath.trimmed().isEmpty() || sameFilesystemTarget(backupPath, m_filepath) || backupInfo.isDir() ||
        backupInfo.isSymLink()) {
        if (error) *error = "Invalid backup path";
        return false;
    }

    sqlite3* rawDb = reinterpret_cast<sqlite3*>(m_db);
    if (sqlite3_get_autocommit(rawDb) == 0) {
        if (error) *error = "Cannot create encrypted backup while an uncommitted transaction is active";
        return false;
    }

    const QString opId = QUuid::createUuid().toString(QUuid::Id128).toLower();
    const QString tempPath = QString("%1.%2.tmp").arg(backupPath, opId);
    const QString walPath = m_filepath + "-wal";

    constexpr int kMaxSnapshotAttempts = 3;
    bool snapshotCaptured = false;
    QString snapshotError;

    for (int attempt = 0; attempt < kMaxSnapshotAttempts; ++attempt) {
        if (!checkpointWalStrict(rawDb, "backup", &snapshotError)) {
            if (error) *error = snapshotError;
            return false;
        }

        (void)shouldInjectFault("after_backup_checkpoint_before_lock");

        char* lockErr = nullptr;
        const int lockRc = sqlite3_exec(rawDb, "BEGIN EXCLUSIVE;", nullptr, nullptr, &lockErr);
        if (lockRc != SQLITE_OK) {
            const QString sqlErr = lockErr ? QString::fromUtf8(lockErr) : QString::fromUtf8(sqlite3_errmsg(rawDb));
            sqlite3_free(lockErr);
            if (error) {
                *error = QString(
                             "Database is busy under competing access; cannot acquire exclusive snapshot lock for "
                             "backup: %1")
                             .arg(sqlErr);
            }
            return false;
        }

        ExclusiveLockGuard lockGuard(rawDb);
        const QFileInfo walInfo(walPath);
        if (walInfo.isSymLink()) {
            if (error) *error = "Refusing backup snapshot when WAL sidecar is a symlink";
            return false;
        }
        if (walInfo.exists() && walInfo.size() > 0) {
            snapshotError = QStringLiteral("Concurrent writes modified WAL during backup snapshot; retry when idle");
            lockGuard.release();
            continue;
        }

        const FileFingerprint beforeStat = captureFileFingerprint(m_filepath);
        QString copyErr;
        if (!copyFileOwnerOnly(m_filepath, tempPath, &copyErr)) {
            QFile::remove(tempPath);
            if (error) *error = copyErr;
            return false;
        }
        const FileFingerprint afterStat = captureFileFingerprint(m_filepath);
        const QFileInfo postWalInfo(walPath);
        if (!beforeStat.exists || beforeStat != afterStat || (postWalInfo.exists() && postWalInfo.size() > 0)) {
            QFile::remove(tempPath);
            snapshotError = QStringLiteral("Source database changed unexpectedly during backup snapshot copy");
            lockGuard.release();
            continue;
        }

        lockGuard.release();
        snapshotCaptured = true;
        break;
    }

    if (!snapshotCaptured) {
        QFile::remove(tempPath);
        if (error) {
            *error = snapshotError.isEmpty() ? QStringLiteral("Failed to capture consistent encrypted backup snapshot")
                                             : snapshotError;
        }
        return false;
    }

    if (!verifyEncryptedFileIntegrity(tempPath, m_password, error, true)) {
        QFile::remove(tempPath);
        return false;
    }

    if (!atomicReplaceFile(tempPath, backupPath, error, "sync_backup_dir")) {
        QFile::remove(tempPath);
        return false;
    }
    if (!QFile::setPermissions(backupPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (error) *error = "Failed to restrict encrypted backup permissions";
        return false;
    }
    return true;
}

bool BookDatabase::recoverInterruptedRestore(const QString& password, QString* error) {
    if (m_filepath.isEmpty() || m_filepath == ":memory:") {
        if (error) *error = "Cannot recover restore state for an in-memory database";
        return false;
    }
    if (m_isOpen) {
        if (error) *error = "Close the database handle before running interrupted restore recovery";
        return false;
    }
    return recoverInterruptedRestoreInternal(password, error);
}

bool BookDatabase::recoverInterruptedRestoreInternal(const QString& passwordToVerify, QString* error) {
    const QString journalPath = restoreJournalPathFor(m_filepath);
    const QFileInfo journalInfo(journalPath);
    if (!journalInfo.exists() && !journalInfo.isSymLink()) {
        return true;
    }

    RestoreJournal journal;
    if (!readRestoreJournal(m_filepath, &journal, error)) {
        return false;
    }

    if (journal.phase == "staging") {
        return recoverStagingPhase(m_filepath, journal, journalPath, error);
    }
    if (journal.phase == "moving_original") {
        return recoverMovingOriginalPhase(m_filepath, journal, journalPath, passwordToVerify, error);
    }
    if (journal.hadOriginalDb) {
        return recoverRollbackWithOriginal(m_filepath, journal, journalPath, passwordToVerify, error);
    }
    return recoverNoOriginalTarget(m_filepath, journal, journalPath, error);
}

bool BookDatabase::restoreEncryptedBackup(const QString& backupPath, const QString& password, bool upgradeSchema,
                                          QString* error) {
    m_lastError.clear();
    auto failWithError = [&](const QString& msg) {
        m_lastError = msg;
        if (error) *error = msg;
        return false;
    };

    if (m_filepath.isEmpty() || m_filepath == ":memory:") {
        return failWithError("Cannot restore file backup into an in-memory database");
    }
    if (QFileInfo(m_filepath).isSymLink()) {
        return failWithError("Refusing to restore over a symlinked database path");
    }
    if (backupPath.trimmed().isEmpty() || sameFilesystemTarget(backupPath, m_filepath)) {
        return failWithError("Invalid backup source path");
    }

    const QString journalPath = restoreJournalPathFor(m_filepath);
    const QFileInfo journalInfo(journalPath);
    if (journalInfo.exists() || journalInfo.isSymLink()) {
        if (m_isOpen) {
            return failWithError(
                QString("Refusing restore while unresolved restore journal %1 exists").arg(journalPath));
        }
        QString recErr;
        if (!recoverInterruptedRestoreInternal(QString(), &recErr)) {
            return failWithError(recErr);
        }
    }

    QString verifyErr;
    if (!verifyEncryptedFileIntegrity(backupPath, password, &verifyErr)) {
        return failWithError(verifyErr);
    }

    if (m_isOpen && m_db) {
        QString walErr;
        if (!checkpointWalStrict(reinterpret_cast<sqlite3*>(m_db), "restore", &walErr)) {
            return failWithError(walErr);
        }
    }

    const QString opId = QUuid::createUuid().toString(QUuid::Id128).toLower();
    const RestorePaths paths = deriveRestorePaths(m_filepath, opId);
    const QString walPath = m_filepath + "-wal";
    const QString shmPath = m_filepath + "-shm";

    RestoreJournal journal;
    journal.opId = opId;
    journal.phase = QStringLiteral("staging");
    journal.targetPath = m_filepath;
    journal.stagedNewPath = paths.stagedNewPath;
    journal.rollbackDbPath = paths.rollbackDbPath;
    journal.rollbackWalPath = paths.rollbackWalPath;
    journal.rollbackShmPath = paths.rollbackShmPath;
    journal.hadOriginalDb = QFile::exists(m_filepath);
    journal.hadOriginalWal = QFile::exists(walPath);
    journal.hadOriginalShm = QFile::exists(shmPath);

    QString journalErr;
    if (!writeRestoreJournal(journal, &journalErr)) {
        return failWithError(journalErr);
    }

    auto cleanupStagingFailure = [&](const QString& failureMessage) {
        QStringList cleanupErrors;
        QString rmErr;
        if (!removeFileChecked(paths.stagedNewPath, "cleanup_staged_new_on_abort", &rmErr)) {
            cleanupErrors.append(rmErr);
        }
        if (!removeFileChecked(journalPath, "cleanup_journal_on_abort", &rmErr)) {
            cleanupErrors.append(rmErr);
        }
        QString syncErr;
        if (!syncParentDirectory(m_filepath, &syncErr, "sync_staging_abort_cleanup")) {
            cleanupErrors.append(syncErr);
        }
        if (!cleanupErrors.isEmpty()) {
            close();
            return failWithError(QString("%1 (cleanup issues: %2)").arg(failureMessage, cleanupErrors.join("; ")));
        }
        return failWithError(failureMessage);
    };

    if (shouldInjectFault("before_stage_backup")) {
        return cleanupStagingFailure("Injected failure before backup staging");
    }

    QString copyErr;
    if (shouldInjectFault("stage_backup") || !copyFileOwnerOnly(backupPath, paths.stagedNewPath, &copyErr)) {
        return cleanupStagingFailure(copyErr.isEmpty() ? QStringLiteral("Failed to stage backup file for restore")
                                                       : copyErr);
    }

    QString stagedSyncErr;
    if (!syncParentDirectory(paths.stagedNewPath, &stagedSyncErr, "sync_staged_backup")) {
        return cleanupStagingFailure(stagedSyncErr);
    }

    if (!verifyEncryptedFileIntegrity(paths.stagedNewPath, password, &verifyErr, true)) {
        return cleanupStagingFailure(verifyErr);
    }

    const QString previousPassword = m_password;
    const bool wasOpen = m_isOpen;
    close();

    auto rollbackAndReopen = [&](const QString& failureMessage) {
        close();
        QString rbErr;
        if (!recoverInterruptedRestoreInternal(wasOpen ? previousPassword : QString(), &rbErr)) {
            return failWithError(QString("%1; rollback failed: %2").arg(failureMessage, rbErr));
        }
        if (wasOpen) {
            QString reopenErr;
            if (!openExactSchema(previousPassword, &reopenErr)) {
                return failWithError(QString("%1; reopen failed: %2").arg(failureMessage, reopenErr));
            }
        }
        return failWithError(failureMessage);
    };

    // Update journal using exact post-close existence of original DB and sidecars
    journal.phase = QStringLiteral("moving_original");
    journal.hadOriginalDb = QFile::exists(m_filepath);
    journal.hadOriginalWal = QFile::exists(walPath);
    journal.hadOriginalShm = QFile::exists(shmPath);
    if (!writeRestoreJournal(journal, &journalErr)) {
        return rollbackAndReopen(journalErr);
    }

    if (journal.hadOriginalDb) {
        if (shouldInjectFault("move_original_db") || !QFile::rename(m_filepath, paths.rollbackDbPath)) {
            return rollbackAndReopen("Failed to move existing database aside during restore");
        }
        QString permErr;
        if (!enforceOwnerOnlyIfExists(paths.rollbackDbPath, "rollback database", &permErr)) {
            return rollbackAndReopen(permErr);
        }
        QString syncErr;
        if (!syncParentDirectory(m_filepath, &syncErr, "sync_after_move_original_db")) {
            return rollbackAndReopen(syncErr);
        }
        if (shouldInjectFault("crash_after_move_original_db")) {
            return failWithError("Simulated crash after moving original database aside");
        }
    }

    if (journal.hadOriginalWal) {
        if (shouldInjectFault("move_original_wal") || !QFile::rename(walPath, paths.rollbackWalPath)) {
            return rollbackAndReopen("Failed to move existing WAL sidecar aside during restore");
        }
        QString permErr;
        if (!enforceOwnerOnlyIfExists(paths.rollbackWalPath, "rollback WAL sidecar", &permErr)) {
            return rollbackAndReopen(permErr);
        }
    }

    if (journal.hadOriginalShm) {
        if (shouldInjectFault("move_original_shm") || !QFile::rename(shmPath, paths.rollbackShmPath)) {
            return rollbackAndReopen("Failed to move existing SHM sidecar aside during restore");
        }
        QString permErr;
        if (!enforceOwnerOnlyIfExists(paths.rollbackShmPath, "rollback SHM sidecar", &permErr)) {
            return rollbackAndReopen(permErr);
        }
    }

    if (journal.hadOriginalWal || journal.hadOriginalShm) {
        QString sidecarSyncErr;
        if (!syncParentDirectory(m_filepath, &sidecarSyncErr, "sync_after_move_original_sidecars")) {
            return rollbackAndReopen(sidecarSyncErr);
        }
    }

    journal.phase = QStringLiteral("placing_replacement");
    if (!writeRestoreJournal(journal, &journalErr)) {
        return rollbackAndReopen(journalErr);
    }
    if (shouldInjectFault("crash_before_place_replacement")) {
        return failWithError("Simulated crash before placing replacement database");
    }

    if (shouldInjectFault("place_replacement") || !QFile::rename(paths.stagedNewPath, m_filepath)) {
        return rollbackAndReopen("Failed to replace database file with backup");
    }
    QString placeSyncErr;
    if (!syncParentDirectory(m_filepath, &placeSyncErr, "sync_after_place_replacement")) {
        return rollbackAndReopen(placeSyncErr);
    }

    if (shouldInjectFault("set_restored_permissions") ||
        !QFile::setPermissions(m_filepath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return rollbackAndReopen("Failed to restrict restored database permissions");
    }

    journal.phase = QStringLiteral("verifying");
    if (!writeRestoreJournal(journal, &journalErr)) {
        return rollbackAndReopen(journalErr);
    }
    if (shouldInjectFault("crash_during_verifying")) {
        return failWithError("Simulated crash during restored database verification");
    }

    if (upgradeSchema) {
        QString upgradeErr;
        if (!openInternal(password, false, &upgradeErr)) {
            return rollbackAndReopen(upgradeErr.isEmpty()
                                         ? QStringLiteral("Failed to open and upgrade restored backup")
                                         : QString("Failed to open and upgrade restored backup: %1").arg(upgradeErr));
        }
    } else {
        QString openErr;
        if (!openExactSchema(password, &openErr)) {
            return rollbackAndReopen(openErr);
        }
    }

    QString commitErr;
    if (!removeFileChecked(journalPath, "remove_restore_journal", &commitErr)) {
        close();
        return failWithError(
            QString(
                "Restored database opened, but failed to remove restore journal (%1); rollback files retained at %2")
                .arg(commitErr, paths.rollbackDbPath));
    }
    if (!syncParentDirectory(journalPath, &commitErr, "sync_after_journal_remove")) {
        close();
        return failWithError(QString("Restored database committed, but directory durability barrier failed (%1); "
                                     "rollback files retained at %2")
                                 .arg(commitErr, paths.rollbackDbPath));
    }

    QStringList cleanupFailures;
    for (const auto& entry :
         {std::pair<QString, QString>{paths.rollbackDbPath, QStringLiteral("cleanup_rollback_db")},
          std::pair<QString, QString>{paths.rollbackWalPath, QStringLiteral("cleanup_rollback_wal")},
          std::pair<QString, QString>{paths.rollbackShmPath, QStringLiteral("cleanup_rollback_shm")},
          std::pair<QString, QString>{paths.stagedNewPath, QStringLiteral("cleanup_staged_new")}}) {
        QString rmErr;
        if (!removeFileChecked(entry.first, entry.second, &rmErr)) {
            cleanupFailures.append(rmErr);
        }
    }
    QString finalSyncErr;
    if (!syncParentDirectory(m_filepath, &finalSyncErr, "sync_after_restore_cleanup")) {
        cleanupFailures.append(finalSyncErr);
    }
    if (!cleanupFailures.isEmpty()) {
        close();
        return failWithError(QString("Restored database committed, but post-commit cleanup or sync failed (%1); "
                                     "remaining recovery files preserved")
                                 .arg(cleanupFailures.join("; ")));
    }
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
                m_lastError = backupError;
                qWarning() << "Pre-migration encrypted backup failed:" << backupError;
                return false;
            }
        }
    }

    QString error;
    if (!runner.run(database, &error)) {
        m_lastError = error;
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
