#include "PromptQueueManager.h"

#include <sqlcipher/sqlite3.h>

#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QVariant>

#include "WalletManager.h"

PromptQueueManager::PromptQueueManager(OllamaClient* ollamaClient, QObject* parent)
    : QObject(parent),
      m_db(nullptr),
      m_ollamaClient(ollamaClient),
      m_isQueuePaused(false),
      m_isGenerating(false),
      m_currentJobId(-1),
      m_activeBookDb(nullptr) {
    m_processTimer = new QTimer(this);
    m_processTimer->setInterval(1000);
    connect(m_processTimer, &QTimer::timeout, this, &PromptQueueManager::processNextJob);

    initDatabase();
    loadJobs();

    // Resume queue if there are pending jobs
    m_processTimer->start();
}

PromptQueueManager::~PromptQueueManager() {
    closeBookDatabase();
    if (m_db) {
        sqlite3_close((sqlite3*)m_db);
        m_db = nullptr;
    }
}

bool PromptQueueManager::initDatabase() {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataDir);
    if (!dir.exists()) dir.mkpath(".");

    QString dbPath = dataDir + "/kllamabooks_queue.db";

    int rc = sqlite3_open(dbPath.toUtf8().constData(), (sqlite3**)&m_db);
    if (rc != SQLITE_OK) {
        qWarning() << "Cannot open queue database: " << sqlite3_errmsg((sqlite3*)m_db);
        return false;
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS jobs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "book_filepath TEXT, "
        "model TEXT, "
        "system_prompt TEXT, "
        "prompt_text TEXT, "
        "parent_message_id INTEGER, "
        "user_message_id INTEGER, "
        "assistant_message_id INTEGER, "
        "status TEXT, "
        "error_text TEXT, "
        "sort_order INTEGER, "
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS notifications ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "book_filepath TEXT, "
        "message_id INTEGER, "
        "type TEXT, "
        "text TEXT, "
        "dismissed INTEGER DEFAULT 0"
        ");";

    char* errMsg = nullptr;
    rc = sqlite3_exec((sqlite3*)m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        qWarning() << "Failed to create queue tables: " << errMsg;
        sqlite3_free(errMsg);
        return false;
    }

    // Reset any Generating jobs to Pending on startup
    const char* resetSql = "UPDATE jobs SET status = 'Pending' WHERE status = 'Generating';";
    sqlite3_exec((sqlite3*)m_db, resetSql, nullptr, nullptr, nullptr);

    return true;
}

void PromptQueueManager::loadJobs() {
    // Notify about counts
    int pendingCount = 0;
    int totalCount = 0;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)m_db,
                           "SELECT COUNT(*) FROM jobs WHERE status = 'Pending' OR status = 'Generating';", -1, &stmt,
                           nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            pendingCount = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2((sqlite3*)m_db, "SELECT COUNT(*) FROM jobs;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            totalCount = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    emit queueUpdated(pendingCount, totalCount);

    // Load notifications
    int unreadCount = 0;
    if (sqlite3_prepare_v2((sqlite3*)m_db, "SELECT COUNT(*) FROM notifications WHERE dismissed = 0;", -1, &stmt,
                           nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            unreadCount = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    emit notificationsUpdated(unreadCount);
}

int PromptQueueManager::getNextSortOrder() {
    int maxOrder = 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)m_db, "SELECT MAX(sort_order) FROM jobs;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            maxOrder = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return maxOrder + 1;
}

int PromptQueueManager::enqueueJob(const QString& bookFilepath, int userMessageId, int assistantMessageId,
                                   const QString& model, const QString& systemPrompt, const QString& promptText) {
    if (!m_db) return -1;

    int sortOrder = getNextSortOrder();

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO jobs (book_filepath, model, system_prompt, prompt_text, parent_message_id, user_message_id, "
        "assistant_message_id, status, sort_order) "
        "VALUES (?, ?, ?, ?, 0, ?, ?, 'Pending', ?);";

    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, bookFilepath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, model.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, systemPrompt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, promptText.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, userMessageId);
    sqlite3_bind_int(stmt, 6, assistantMessageId);
    sqlite3_bind_int(stmt, 7, sortOrder);

    int newId = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        newId = sqlite3_last_insert_rowid((sqlite3*)m_db);
    }
    sqlite3_finalize(stmt);

    loadJobs();
    return newId;
}

void PromptQueueManager::pauseQueue() {
    m_isQueuePaused = true;
    if (m_isGenerating && m_currentJobId != -1) {
        // Stop current generation and mark as Pending again, or leave it running?
        // Let's cancel the current job generation and put it back to pending
        // Ollama Client has no built-in cancel? Wait, MainWindow used m_generationId to ignore chunks.
        // We'll just ignore chunks by incrementing generation ID or resetting state.
        // Wait, OllamaClient does not provide a cancel method for the network request.
        // We can just set status to "Pending" and ignore the rest of the chunks.
        m_isGenerating = false;
        updateJobStatus(m_currentJobId, "Paused");
        closeBookDatabase();
        m_currentJobId = -1;
    }
}

void PromptQueueManager::resumeQueue() {
    m_isQueuePaused = false;
    // Set all paused jobs back to Pending? Or just unpause the queue processing?
    // Unpause queue processing.
}

bool PromptQueueManager::isQueuePaused() const { return m_isQueuePaused; }

void PromptQueueManager::pauseJob(int jobId) {
    if (jobId == m_currentJobId) {
        m_isGenerating = false;
        closeBookDatabase();
        m_currentJobId = -1;
    }
    updateJobStatus(jobId, "Paused");
}

void PromptQueueManager::resumeJob(int jobId) { updateJobStatus(jobId, "Pending"); }

void PromptQueueManager::cancelJob(int jobId) {
    if (jobId == m_currentJobId) {
        m_isGenerating = false;
        closeBookDatabase();
        m_currentJobId = -1;
    }
    updateJobStatus(jobId, "Canceled");
}

void PromptQueueManager::moveJobUp(int jobId) {
    // Implement sort_order swapping
    // Get current sort order
    sqlite3_stmt* stmt = nullptr;
    int currentSort = -1;
    if (sqlite3_prepare_v2((sqlite3*)m_db, "SELECT sort_order FROM jobs WHERE id = ?;", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, jobId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            currentSort = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    if (currentSort == -1) return;

    // Get previous job
    int prevId = -1;
    int prevSort = -1;
    if (sqlite3_prepare_v2((sqlite3*)m_db,
                           "SELECT id, sort_order FROM jobs WHERE sort_order < ? ORDER BY sort_order DESC LIMIT 1;", -1,
                           &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, currentSort);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            prevId = sqlite3_column_int(stmt, 0);
            prevSort = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    if (prevId == -1) return;

    // Swap
    QString sql = QString("UPDATE jobs SET sort_order = %1 WHERE id = %2;").arg(prevSort).arg(jobId);
    sqlite3_exec((sqlite3*)m_db, sql.toUtf8().constData(), nullptr, nullptr, nullptr);
    sql = QString("UPDATE jobs SET sort_order = %1 WHERE id = %2;").arg(currentSort).arg(prevId);
    sqlite3_exec((sqlite3*)m_db, sql.toUtf8().constData(), nullptr, nullptr, nullptr);
    loadJobs();
}

void PromptQueueManager::moveJobDown(int jobId) {
    // Implement sort_order swapping
    sqlite3_stmt* stmt = nullptr;
    int currentSort = -1;
    if (sqlite3_prepare_v2((sqlite3*)m_db, "SELECT sort_order FROM jobs WHERE id = ?;", -1, &stmt, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, jobId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            currentSort = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    if (currentSort == -1) return;

    int nextId = -1;
    int nextSort = -1;
    if (sqlite3_prepare_v2((sqlite3*)m_db,
                           "SELECT id, sort_order FROM jobs WHERE sort_order > ? ORDER BY sort_order ASC LIMIT 1;", -1,
                           &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, currentSort);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            nextId = sqlite3_column_int(stmt, 0);
            nextSort = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    if (nextId == -1) return;

    // Swap
    QString sql = QString("UPDATE jobs SET sort_order = %1 WHERE id = %2;").arg(nextSort).arg(jobId);
    sqlite3_exec((sqlite3*)m_db, sql.toUtf8().constData(), nullptr, nullptr, nullptr);
    sql = QString("UPDATE jobs SET sort_order = %1 WHERE id = %2;").arg(currentSort).arg(nextId);
    sqlite3_exec((sqlite3*)m_db, sql.toUtf8().constData(), nullptr, nullptr, nullptr);
    loadJobs();
}

void PromptQueueManager::updateJobStatus(int jobId, const QString& status, const QString& errorText) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)m_db, "UPDATE jobs SET status = ?, error_text = ? WHERE id = ?;", -1, &stmt,
                           nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, status.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, errorText.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, jobId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    emit jobStatusChanged(jobId, status);
    loadJobs();
}

void PromptQueueManager::processNextJob() {
    if (m_isQueuePaused || m_isGenerating) return;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, book_filepath, model, system_prompt, prompt_text, assistant_message_id FROM jobs WHERE status = "
        "'Pending' ORDER BY sort_order ASC LIMIT 1;";

    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            m_currentJobId = sqlite3_column_int(stmt, 0);
            QString bookFilepath = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
            QString model = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
            QString systemPrompt = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
            QString promptText = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
            int assistantMessageId = sqlite3_column_int(stmt, 5);

            sqlite3_finalize(stmt);

            // Open target database
            if (!openBookDatabase(bookFilepath)) {
                // Cannot process this job because password is missing or file not found
                updateJobStatus(m_currentJobId, "Error",
                                "Failed to open book database. Please ensure it is unlocked or password is saved.");
                m_currentJobId = -1;
                return;
            }

            updateJobStatus(m_currentJobId, "Generating");
            m_isGenerating = true;
            m_currentFullText = "";
            m_activeBookFilepath = bookFilepath;

            m_ollamaClient->setSystemPrompt(systemPrompt);
            m_ollamaClient->generate(
                model, promptText,
                [this, bookFilepath, assistantMessageId](const QString& chunk) {
                    if (!m_isGenerating) return;  // job was canceled/paused
                    m_currentFullText += chunk;
                    updateBookMessage(assistantMessageId, m_currentFullText);
                    emit chunkReceived(m_currentJobId, bookFilepath, assistantMessageId, chunk, m_currentFullText);
                },
                [this, bookFilepath, assistantMessageId](const QString& fullText) {
                    if (!m_isGenerating) return;
                    m_isGenerating = false;
                    updateBookMessage(assistantMessageId, fullText);
                    updateJobStatus(m_currentJobId, "Completed");
                    addNotification(bookFilepath, assistantMessageId, "responded", "Chat updated with response.");
                    emit jobCompleted(m_currentJobId, bookFilepath, assistantMessageId, fullText);
                    closeBookDatabase();
                    m_currentJobId = -1;
                },
                [this, bookFilepath, assistantMessageId](const QString& errorText) {
                    if (!m_isGenerating) return;
                    m_isGenerating = false;
                    QString finalErr =
                        QString("<br/><span style='color:red;'>[ERROR: %1]</span>").arg(errorText.toHtmlEscaped());
                    m_currentFullText += finalErr;
                    updateBookMessage(assistantMessageId, m_currentFullText);
                    updateJobStatus(m_currentJobId, "Error", errorText);
                    addNotification(bookFilepath, assistantMessageId, "error", "Chat response failed: " + errorText);
                    emit jobError(m_currentJobId, bookFilepath, assistantMessageId, errorText);
                    closeBookDatabase();
                    m_currentJobId = -1;
                });
            return;
        }
        sqlite3_finalize(stmt);
    }
}

void PromptQueueManager::onChunkReceived(const QString& chunk) {}

void PromptQueueManager::onGenerationComplete(const QString& fullText) {}

void PromptQueueManager::onGenerationError(const QString& error) {}

bool PromptQueueManager::openBookDatabase(const QString& filepath) {
    if (m_activeBookDb) closeBookDatabase();

    int rc = sqlite3_open(filepath.toUtf8().constData(), (sqlite3**)&m_activeBookDb);
    if (rc != SQLITE_OK) {
        m_activeBookDb = nullptr;
        return false;
    }

    QFileInfo fileInfo(filepath);
    QString password = WalletManager::loadPassword(fileInfo.fileName());

    if (!password.isEmpty()) {
        QString escapedPassword = password;
        escapedPassword.replace("'", "''");
        QString keyPragma = QString("PRAGMA key = '%1';").arg(escapedPassword);
        char* errMsg = nullptr;
        rc = sqlite3_exec((sqlite3*)m_activeBookDb, keyPragma.toUtf8().constData(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            sqlite3_free(errMsg);
            closeBookDatabase();
            return false;
        }
    }

    char* errMsg = nullptr;
    rc = sqlite3_exec((sqlite3*)m_activeBookDb, "SELECT count(*) FROM sqlite_master;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        sqlite3_free(errMsg);
        closeBookDatabase();
        return false;
    }

    return true;
}

void PromptQueueManager::closeBookDatabase() {
    if (m_activeBookDb) {
        sqlite3_close((sqlite3*)m_activeBookDb);
        m_activeBookDb = nullptr;
    }
}

bool PromptQueueManager::updateBookMessage(int messageId, const QString& content) {
    if (!m_activeBookDb) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE messages SET content = ? WHERE id = ?;";

    if (sqlite3_prepare_v2((sqlite3*)m_activeBookDb, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, messageId);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

void PromptQueueManager::addNotification(const QString& bookFilepath, int messageId, const QString& type,
                                         const QString& text) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO notifications (book_filepath, message_id, type, text) VALUES (?, ?, ?, ?);";

    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, bookFilepath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, messageId);
        sqlite3_bind_text(stmt, 3, type.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, text.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    loadJobs();  // triggers notification count update
}

void PromptQueueManager::dismissNotification(int notificationId) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE notifications SET dismissed = 1 WHERE id = ?;";

    if (sqlite3_prepare_v2((sqlite3*)m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, notificationId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    loadJobs();
}

QList<QueueJob> PromptQueueManager::getJobs() const {
    QList<QueueJob> jobs;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            (sqlite3*)m_db,
            "SELECT id, book_filepath, model, system_prompt, prompt_text, parent_message_id, user_message_id, "
            "assistant_message_id, status, error_text, sort_order FROM jobs ORDER BY sort_order ASC;",
            -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            QueueJob job;
            job.id = sqlite3_column_int(stmt, 0);
            job.bookFilepath = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
            job.model = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
            job.systemPrompt = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
            job.promptText = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
            job.parentMessageId = sqlite3_column_int(stmt, 5);
            job.userMessageId = sqlite3_column_int(stmt, 6);
            job.assistantMessageId = sqlite3_column_int(stmt, 7);
            job.status = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 8));
            const char* err = (const char*)sqlite3_column_text(stmt, 9);
            job.errorText = err ? QString::fromUtf8(err) : QString();
            job.sortOrder = sqlite3_column_int(stmt, 10);
            jobs.append(job);
        }
        sqlite3_finalize(stmt);
    }
    return jobs;
}

QList<QueueNotification> PromptQueueManager::getUnreadNotifications() const {
    QList<QueueNotification> notifs;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            (sqlite3*)m_db,
            "SELECT id, book_filepath, message_id, type, text FROM notifications WHERE dismissed = 0 ORDER BY id DESC;",
            -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            QueueNotification n;
            n.id = sqlite3_column_int(stmt, 0);
            n.bookFilepath = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
            n.messageId = sqlite3_column_int(stmt, 2);
            n.type = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
            n.text = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
            n.dismissed = false;
            notifs.append(n);
        }
        sqlite3_finalize(stmt);
    }
    return notifs;
}

int PromptQueueManager::currentGeneratingJobId() const { return m_currentJobId; }
