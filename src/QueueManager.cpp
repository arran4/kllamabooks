#include "QueueManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QRegularExpression>
#include <QTimer>

QueueManager& QueueManager::instance() {
    static QueueManager instance;
    return instance;
}

QueueManager::QueueManager(QObject* parent) : QObject(parent), m_timer(new QTimer(this)), m_isPaused(false) {
    connect(m_timer, &QTimer::timeout, this, &QueueManager::checkQueue);
    m_timer->start(1000);  // Check every second
}

void QueueManager::addDatabase(std::shared_ptr<BookDatabase> db) {
    if (!m_databases.contains(db)) {
        m_databases.append(db);
        emit queueChanged();
        QTimer::singleShot(0, this, &QueueManager::checkQueue);
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

void QueueManager::retryItem(std::shared_ptr<BookDatabase> db, int queueId) {
    if (db) {
        db->updateQueueError(queueId, "");
        emit queueChanged();
        QTimer::singleShot(0, this, &QueueManager::checkQueue);
    }
}

void QueueManager::modifyItem(std::shared_ptr<BookDatabase> db, int queueId, const QString& newPrompt) {
    if (db) {
        db->updateQueueItemPrompt(queueId, newPrompt);
        db->updateQueueError(queueId, "");
        emit queueChanged();
        QTimer::singleShot(0, this, &QueueManager::checkQueue);
    }
}

void QueueManager::setActiveDatabase(std::shared_ptr<BookDatabase> db) {
    if (m_activeDb != db) {
        m_activeDb = db;
        // Optionally trigger check immediately
        QTimer::singleShot(0, this, &QueueManager::checkQueue);
    }
}

void QueueManager::setClient(OllamaClient* client) { m_client = client; }

/**
 * @brief Appends a generation request payload to the processing queue.
 *
 * @param messageId The target chat node ID.
 * @param model The LLM string identifier.
 * @param prompt The prompt context payload.
 * @param priority The priority ranking.
 * @param targetType The semantic class of generation (e.g. document, message).
 */
void QueueManager::enqueuePrompt(int messageId, const QString& model, const QString& prompt, int priority,
                                 const QString& targetType, int parentId) {
    if (m_activeDb && m_activeDb->isOpen()) {
        m_activeDb->enqueuePrompt(messageId, model, prompt, priority, targetType, parentId);
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
        if (item.processingId == 0 && item.lastError.isEmpty()) count++;
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
    std::sort(result.begin(), result.end(), [](const MergedQueueItem& a, const MergedQueueItem& b) {
        if (a.item.priority != b.item.priority) return a.item.priority > b.item.priority;
        return a.item.timestamp > b.item.timestamp;
    });
    return result;
}

/**
 * @brief Explicitly issues a termination request to the active Ollama network generation.
 */
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
    for (auto db : m_databases) {
        if (!db || !db->isOpen()) continue;
        auto items = db->getQueue();
        for (const auto& item : items) {
            if (item.state == "completed" || item.state == "error") {
                if (item.targetType == "document" && item.state == "completed") {
                    // Do not delete completed document generations automatically so user can review them.
                    continue;
                }
                db->deleteQueueItem(item.id);
            }
        }
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

/**
 * @brief Advances the queue consumer to begin execution of the next pending element.
 */
void QueueManager::processNext() {
    if (m_isProcessing) return;

    QSettings settings;
    QString processingMode = settings.value("queueProcessing", "FCFS").toString();
    bool prioritizeSameModel = settings.value("prioritizeSameModel", false).toBool();

    QList<QueueManager::MergedQueueItem> allPending;
    for (auto db : m_databases) {
        if (!db || !db->isOpen()) continue;
        auto items = db->getQueue();
        for (const auto& item : items) {
            if (item.processingId == 0 && item.lastError.isEmpty()) {
                allPending.append({db, item});
            }
        }
    }

    if (allPending.isEmpty()) return;

    auto getModelSize = [](const QString& model) -> double {
        QRegularExpression re("([0-9]+(?:\\.[0-9]+)?)[bB]");
        QRegularExpressionMatch match = re.match(model);
        if (match.hasMatch()) {
            return match.captured(1).toDouble();
        }
        return 0.0;
    };

    QString lastModel = m_lastProcessedModel;
    std::sort(allPending.begin(), allPending.end(),
              [processingMode, getModelSize, prioritizeSameModel, lastModel](const QueueManager::MergedQueueItem& a,
                                                                             const QueueManager::MergedQueueItem& b) {
                  if (a.item.priority != b.item.priority) {
                      return a.item.priority > b.item.priority;
                  }

                  if (prioritizeSameModel && !lastModel.isEmpty()) {
                      bool aMatches = (a.item.model == lastModel);
                      bool bMatches = (b.item.model == lastModel);
                      if (aMatches && !bMatches) return true;
                      if (!aMatches && bMatches) return false;
                  }

                  if (processingMode == "LCFS") {
                      return a.item.timestamp > b.item.timestamp;
                  } else if (processingMode == "Smallest message first") {
                      return a.item.prompt.length() < b.item.prompt.length();
                  } else if (processingMode == "Largest message first") {
                      return a.item.prompt.length() > b.item.prompt.length();
                  } else if (processingMode == "Smallest model first" || processingMode == "Largest model first") {
                      double sizeA = getModelSize(a.item.model);
                      double sizeB = getModelSize(b.item.model);

                      if (sizeA != sizeB) {
                          if (processingMode == "Smallest model first") {
                              return sizeA < sizeB;
                          } else {
                              return sizeA > sizeB;
                          }
                      } else {
                          return a.item.model.compare(b.item.model) < 0;  // Fallback to string comparison
                      }
                  } else {  // FCFS
                      return a.item.timestamp < b.item.timestamp;
                  }
              });

    auto nextItem = allPending.first();
    m_currentDb = nextItem.db;
    m_currentItem = nextItem.item;

    m_isProcessing = true;
    m_currentItem.processingId = QCoreApplication::applicationPid();
    m_currentDb->updateQueueProcessingId(m_currentItem.id, m_currentItem.processingId);
    m_currentDb->updateQueueItemState(m_currentItem.id, "processing");
    m_currentItem.state = "processing";
    m_lastProcessedModel = m_currentItem.model;
    emit processingStarted(m_currentDb, m_currentItem.messageId, m_currentItem.targetType);
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

    m_client->generate(
        m_currentItem.model, m_currentItem.prompt, [this](const QString& chunk) { onChunk(chunk); },
        [this](const QString& response) { onComplete(response); }, [this](const QString& error) { onError(error); });
}

/**
 * @brief Triggered whenever the Ollama backend streams a partial string sequence.
 *
 * @param chunk The text sequence slice.
 */
void QueueManager::onChunk(const QString& chunk) {
    if (!m_isProcessing) return;
    if (m_currentDb && m_currentDb->isOpen()) {
        QString currentContent;
        if (m_currentItem.targetType == "document") {
            // For documents, we do not update the document directly.
            // Just emit the chunk for the preview.
        } else {
            // Get existing content and append
            auto messages = m_currentDb->getMessages();
            for (const auto& m : messages) {
                if (m.id == m_currentItem.messageId) {
                    currentContent = m.content;
                    break;
                }
            }
            m_currentDb->updateMessage(m_currentItem.messageId, currentContent + chunk);
        }
        emit processingChunk(m_currentDb, m_currentItem.messageId, chunk, m_currentItem.targetType);
    }
}

/**
 * @brief Triggered when the Ollama backend has successfully closed the generation stream.
 *
 * @param response The complete generated text payload.
 */
void QueueManager::onComplete(const QString& response) {
    if (!m_isProcessing) return;
    if (m_currentDb && m_currentDb->isOpen()) {
        if (m_currentItem.targetType == "document") {
            // Document results are queued for review, NOT applied immediately.
            m_currentDb->updateQueueItemState(m_currentItem.id, "completed", response);
            m_currentDb->addNotification(m_currentItem.id, "review_needed");
        } else {
            m_currentDb->updateMessage(m_currentItem.messageId, response);
            m_currentDb->deleteQueueItem(m_currentItem.id);
            m_currentDb->addNotification(m_currentItem.messageId, "responded_to");
        }

        m_currentItem.processingId = 0;

        // Remove tracking completed items in memory to let DB act as source of truth.
        // m_completedItems.append({m_currentDb, m_currentItem}); // deprecated

        m_currentDb->setSetting(m_currentItem.targetType, m_currentItem.messageId, "model", m_currentItem.model);

        QString sysPrompt = m_currentDb->getInheritedSetting(m_currentItem.messageId, "systemPrompt");
        if (sysPrompt.isEmpty()) {
            sysPrompt = m_currentDb->getSetting("book", 0, "systemPrompt", "");
        }
        if (sysPrompt.isEmpty()) {
            QSettings settings;
            sysPrompt = settings.value("globalSystemPrompt", "").toString();
        }
        m_currentDb->setSetting(m_currentItem.targetType, m_currentItem.messageId, "systemPrompt", sysPrompt);
    }
    m_isProcessing = false;
    emit processingFinished(m_currentDb, m_currentItem.messageId, true, m_currentItem.targetType);
    emit queueChanged();

    // Pick up next item after a short delay to avoid tight loop issues
    QTimer::singleShot(500, this, &QueueManager::checkQueue);
}

/**
 * @brief Triggered when an exception or HTTP abort is thrown by the Ollama backend.
 *
 * @param error The descriptive error message.
 */
void QueueManager::onError(const QString& error) {
    if (!m_isProcessing) return;
    if (m_currentDb && m_currentDb->isOpen()) {
        m_currentDb->updateQueueError(m_currentItem.id, error);
        m_currentDb->addNotification(m_currentItem.messageId, "error");
    }
    m_isProcessing = false;
    emit processingFinished(m_currentDb, m_currentItem.messageId, false, m_currentItem.targetType);
    emit queueChanged();

    QTimer::singleShot(500, this, &QueueManager::checkQueue);
}
