#include "QueueManager.h"
#include <QDebug>
#include <QTimer>

QueueManager& QueueManager::instance() {
    static QueueManager instance;
    return instance;
}

QueueManager::QueueManager(QObject* parent) 
    : QObject(parent), m_timer(new QTimer(this)), m_isPaused(false) {
    connect(m_timer, &QTimer::timeout, this, &QueueManager::checkQueue);
    m_timer->start(1000); // Check every second
}

void QueueManager::addDatabase(std::shared_ptr<BookDatabase> db) {
    if (!m_databases.contains(db)) {
        m_databases.append(db);
        emit queueChanged();
    }
}

void QueueManager::removeDatabase(std::shared_ptr<BookDatabase> db) {
    m_databases.removeAll(db);
    if (m_activeDb == db) m_activeDb = nullptr;
    if (m_currentDb == db) {
        m_currentDb = nullptr;
        m_isProcessing = false;
    }
    emit queueChanged();
}

void QueueManager::setActiveDatabase(std::shared_ptr<BookDatabase> db) {
    if (m_activeDb != db) {
        m_activeDb = db;
        // Optionally trigger check immediately
        QTimer::singleShot(0, this, &QueueManager::checkQueue);
    }
}

void QueueManager::setClient(OllamaClient* client) {
    m_client = client;
}

void QueueManager::enqueuePrompt(int messageId, const QString& model, const QString& prompt, int priority) {
    if (m_activeDb && m_activeDb->isOpen()) {
        m_activeDb->enqueuePrompt(messageId, model, prompt, priority);
        emit queueChanged();
        QTimer::singleShot(0, this, &QueueManager::checkQueue);
    }
}

int QueueManager::totalPendingCount() const {
    int total = 0;
    for (const auto& db : m_databases) {
        total += pendingCount(db);
    }
    return total;
}

int QueueManager::pendingCount(std::shared_ptr<BookDatabase> db) const {
    if (!db || !db->isOpen()) return 0;
    auto queue = db->getQueue();
    int count = 0;
    for (const auto& item : queue) {
        if (item.status == "pending") count++;
    }
    return count;
}

void QueueManager::checkQueue() {
    if (m_isProcessing || m_isPaused || !m_client || m_databases.isEmpty()) return;
    processNext();
}

QList<QueueManager::MergedQueueItem> QueueManager::getMergedQueue() const {
    QList<MergedQueueItem> result;
    for (auto db : m_databases) {
        if (!db || !db->isOpen()) continue;
        auto items = db->getQueue();
        for (const auto& item : items) {
            result.append({db, item});
        }
    }
    // Sort merged result by priority then timestamp
    std::sort(result.begin(), result.end(), [](const MergedQueueItem& a, const MergedQueueItem& b){
        if (a.item.priority != b.item.priority) return a.item.priority > b.item.priority;
        return a.item.timestamp < b.item.timestamp;
    });
    return result;
}

void QueueManager::cancelItem(std::shared_ptr<BookDatabase> db, int queueId) {
    if (db) db->deleteQueueItem(queueId);
    if (m_currentDb == db && m_currentItem.id == queueId) {
        // Ideally we'd abort the network request here
        m_isProcessing = false;
        m_currentDb = nullptr;
    }
    emit queueChanged();
}

void QueueManager::clearCompleted() {
    for (const auto& db : m_databases) {
        if (db) db->clearCompletedQueue();
    }
    emit queueChanged();
}

void QueueManager::pauseQueue() {
    m_isPaused = true;
    emit queueChanged();
}

void QueueManager::resumeQueue() {
    m_isPaused = false;
    emit queueChanged();
    QTimer::singleShot(0, this, &QueueManager::checkQueue);
}

void QueueManager::processNext() {
    if (m_isProcessing) return;

    // 1. Try active database first
    if (m_activeDb && m_activeDb->isOpen()) {
        auto queue = m_activeDb->getQueue();
        for (const auto& item : queue) {
            if (item.status == "pending") {
                m_currentDb = m_activeDb;
                m_currentItem = item;
                goto found;
            }
        }
    }

    // 2. Round-robin through others
    if (m_databases.isEmpty()) return;
    for (int i = 0; i < m_databases.size(); ++i) {
        m_currentIndex = (m_currentIndex + 1) % m_databases.size();
        auto db = m_databases[m_currentIndex];
        if (db == m_activeDb) continue;
        if (!db || !db->isOpen()) continue;

        auto queue = db->getQueue();
        for (const auto& item : queue) {
            if (item.status == "pending") {
                m_currentDb = db;
                m_currentItem = item;
                goto found;
            }
        }
    }
    return;

found:
    m_isProcessing = true;
    m_currentDb->updateQueueStatus(m_currentItem.id, "processing");
    emit processingStarted(m_currentDb, m_currentItem.messageId);
    emit queueChanged();

    QString sysPrompt = m_currentDb->getInheritedSetting(m_currentItem.messageId, "systemPrompt");
    if (sysPrompt.isEmpty()) {
        sysPrompt = m_currentDb->getSetting("book", 0, "systemPrompt", "");
    }
    if (sysPrompt.isEmpty()) {
        QSettings settings;
        sysPrompt = settings.value("globalSystemPrompt", "").toString();
    }
    m_client->setSystemPrompt(sysPrompt);

    m_client->generate(m_currentItem.model, m_currentItem.prompt,
                       [this](const QString& chunk){ onChunk(chunk); },
                       [this](const QString& response){ onComplete(response); },
                       [this](const QString& error){ onError(error); });
}

void QueueManager::onChunk(const QString& chunk) {
    if (!m_isProcessing) return;
    if (m_currentDb && m_currentDb->isOpen()) {
        // Get existing content and append
        auto messages = m_currentDb->getMessages();
        QString currentContent;
        for (const auto& m : messages) {
            if (m.id == m_currentItem.messageId) {
                currentContent = m.content;
                break;
            }
        }
        m_currentDb->updateMessage(m_currentItem.messageId, currentContent + chunk);
        emit processingChunk(m_currentDb, m_currentItem.messageId, chunk);
    }
}

void QueueManager::onComplete(const QString& response) {
    if (!m_isProcessing) return;
    if (m_currentDb && m_currentDb->isOpen()) {
        m_currentDb->updateMessage(m_currentItem.messageId, response);
        m_currentDb->updateQueueStatus(m_currentItem.id, "completed");
        m_currentDb->addNotification(m_currentItem.messageId, "responded_to");
    }
    m_isProcessing = false;
    emit processingFinished(m_currentDb, m_currentItem.messageId, true);
    emit queueChanged();
    
    // Pick up next item after a short delay to avoid tight loop issues
    QTimer::singleShot(500, this, &QueueManager::checkQueue);
}

void QueueManager::onError(const QString& error) {
    if (!m_isProcessing) return;
    if (m_currentDb && m_currentDb->isOpen()) {
        m_currentDb->updateQueueStatus(m_currentItem.id, "error");
        m_currentDb->addNotification(m_currentItem.messageId, "error");
    }
    m_isProcessing = false;
    emit processingFinished(m_currentDb, m_currentItem.messageId, false);
    emit queueChanged();
    
    QTimer::singleShot(500, this, &QueueManager::checkQueue);
}
