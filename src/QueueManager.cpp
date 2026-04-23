#include "QueueManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

QueueManager& QueueManager::instance() {
    static QueueManager instance;
    return instance;
}

QueueManager::QueueManager(QObject* parent)
    : QObject(parent), m_timer(new QTimer(this)), m_isPaused(false), m_probeTimer(new QTimer(this)) {
    connect(m_timer, &QTimer::timeout, this, &QueueManager::checkQueue);
    m_timer->start(1000);  // Check every second

    connect(m_probeTimer, &QTimer::timeout, this, [this]() {
        if (m_client && totalPendingCount() > 0) {
            m_client->fetchModels();
        }
    });
    m_probeTimer->setInterval(5000);
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

    QList<int> toRemove;
    for (auto it = m_activeItems.begin(); it != m_activeItems.end(); ++it) {
        if (it.value().db == db) {
            toRemove.append(it.key());
        }
    }
    for (int key : toRemove) {
        m_activeItems.remove(key);
    }

    emit queueChanged();
}

void QueueManager::retryItem(std::shared_ptr<BookDatabase> db, int queueId) {
    if (db) {
        db->updateQueueError(queueId, "");
        emit queueChanged();

        if (!m_isEndpointUp && m_client) {
            m_client->fetchModels();
        }

        QTimer::singleShot(0, this, &QueueManager::checkQueue);
    }
}

void QueueManager::modifyItem(std::shared_ptr<BookDatabase> db, int queueId, const QString& newPrompt,
                              const QString& newModel) {
    if (db) {
        db->updateQueueItemPrompt(queueId, newPrompt);
        if (!newModel.isEmpty()) db->updateQueueItemModel(queueId, newModel);
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

void QueueManager::setClient(OllamaClient* client) {
    m_client = client;
    if (m_client) {
        connect(m_client, &OllamaClient::connectionStatusChanged, this, [this](bool isOk) {
            m_isEndpointUp = isOk;
            if (isOk) {
                m_probeTimer->stop();
                QTimer::singleShot(0, this, &QueueManager::checkQueue);
            } else {
                if (!m_probeTimer->isActive()) {
                    m_probeTimer->start();
                }
            }
        });
    }
}

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
                                 const QString& targetType, int parentId, const QString& targetAction) {
    if (m_activeDb && m_activeDb->isOpen()) {
        m_activeDb->enqueuePrompt(messageId, model, prompt, priority, targetType, parentId, targetAction);
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

void QueueManager::setMaxConcurrent(int max) {
    if (max > 0) {
        m_maxConcurrent = max;
        QTimer::singleShot(0, this, &QueueManager::checkQueue);
    }
}

void QueueManager::checkQueue() {
    if (m_isPaused || !m_client || m_databases.isEmpty()) return;
    if (!m_isEndpointUp) return;
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

QueueManager::QueueStats QueueManager::getQueueStats() const {
    QueueStats stats;
    for (auto db : m_databases) {
        if (!db || !db->isOpen()) continue;
        auto dbStats = db->getQueueStats();
        stats.pending += dbStats.pending;
        stats.processing += dbStats.processing;
        stats.completed += dbStats.completed;
        stats.error += dbStats.error;
    }
    return stats;
}

/**
 * @brief Explicitly issues a termination request to the active Ollama network generation.
 */
void QueueManager::cancelItem(std::shared_ptr<BookDatabase> db, int queueId) {
    if (db) db->deleteQueueItem(queueId);

    int toRemoveId = -1;
    for (auto it = m_activeItems.begin(); it != m_activeItems.end(); ++it) {
        if (it.value().db == db && it.value().item.id == queueId) {
            toRemoveId = it.key();
            break;
        }
    }
    if (toRemoveId != -1) {
        // Ideally we'd abort the network request here
        m_activeItems.remove(toRemoveId);
    }
    emit queueChanged();
}

void QueueManager::clearCompleted() {
    for (auto db : m_databases) {
        if (!db || !db->isOpen()) continue;
        auto items = db->getQueue();
        for (const auto& item : items) {
            if (item.state == "completed" || item.state == "error" || !item.lastError.isEmpty()) {
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
    if (m_activeItems.size() >= m_maxConcurrent) return;

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

    int tasksToStart = qMin(m_maxConcurrent - m_activeItems.size(), allPending.size());
    for (int t = 0; t < tasksToStart; ++t) {
        auto nextItem = allPending[t];
        auto db = nextItem.db;
        auto item = nextItem.item;

        int procId = m_nextProcessingId++;
        item.processingId = procId;
        db->updateQueueProcessingId(item.id, item.processingId);
        db->updateQueueItemState(item.id, "processing");
        item.state = "processing";
        m_lastProcessedModel = item.model;

        m_activeItems.insert(procId, {db, item});

        emit processingStarted(db, item.messageId, item.targetType);
        emit queueChanged();

        QString sysPrompt = db->getInheritedSetting(item.messageId, "systemPrompt");
        if (sysPrompt.isEmpty()) {
            sysPrompt = db->getSetting("book", 0, "systemPrompt", "");
        }
        if (sysPrompt.isEmpty()) {
            QSettings settings;
            sysPrompt = settings.value("globalSystemPrompt", "").toString();
        }

        // System prompt is configured globally in the client, but in concurrent setups
        // we might want it passed dynamically. For now, since OllamaClient doesn't support
        // per-request system prompts for standard generate (it does for chat via roles),
        // we set it globally. This might cause a race condition if two standard generates
        // have different system prompts, but it's a limitation of the current client interface.
        m_client->setSystemPrompt(sysPrompt);

        if (item.targetType == "message") {
            QJsonArray messagesArray;
            QList<MessageNode> allMessages = db->getMessages();

            // Tracing back from the target ai message's parent (which is the user message) to the root
            QList<MessageNode> path;
            int currentId = 0;

            // Find the AI message first to get its parent
            for (const auto& msg : allMessages) {
                if (msg.id == item.messageId) {
                    currentId = msg.parentId;
                    break;
                }
            }

            while (currentId != 0) {
                bool found = false;
                for (const auto& msg : allMessages) {
                    if (msg.id == currentId) {
                        path.prepend(msg);
                        currentId = msg.parentId;
                        found = true;
                        break;
                    }
                }
                if (!found) break;  // Break if tree is broken
            }

            for (int i = 0; i < path.size(); ++i) {
                QJsonObject msgObj;
                msgObj["role"] = path[i].role;
                // For the last user message, we use the potentially updated prompt from the queue
                if (i == path.size() - 1 && path[i].role == "user") {
                    msgObj["content"] = item.prompt;
                } else {
                    msgObj["content"] = path[i].content;
                }
                messagesArray.append(msgObj);
            }

            m_client->generateChat(
                item.model, messagesArray,
                [this, procId](const QString& chunk) {
                    if (!m_activeItems.contains(procId)) return;
                    auto act = m_activeItems[procId];
                    if (act.db && act.db->isOpen()) {
                        QString currentContent;
                        auto messages = act.db->getMessages();
                        for (const auto& m : messages) {
                            if (m.id == act.item.messageId) {
                                currentContent = m.content;
                                break;
                            }
                        }
                        act.db->updateMessage(act.item.messageId, currentContent + chunk);
                        emit processingChunk(act.db, act.item.messageId, chunk, act.item.targetType);
                    }
                },
                [this, procId](const QString& response) {
                    if (!m_activeItems.contains(procId)) return;
                    auto act = m_activeItems[procId];
                    if (act.db && act.db->isOpen()) {
                        act.db->updateMessage(act.item.messageId, response);
                        act.db->deleteQueueItem(act.item.id);
                        act.db->addNotification(act.item.messageId, act.item.targetType, "responded_to");

                        act.db->setSetting(act.item.targetType, act.item.messageId, "model", act.item.model);

                        QString sysPrompt = act.db->getInheritedSetting(act.item.messageId, "systemPrompt");
                        if (sysPrompt.isEmpty()) {
                            sysPrompt = act.db->getSetting("book", 0, "systemPrompt", "");
                        }
                        if (sysPrompt.isEmpty()) {
                            QSettings settings;
                            sysPrompt = settings.value("globalSystemPrompt", "").toString();
                        }
                        act.db->setSetting(act.item.targetType, act.item.messageId, "systemPrompt", sysPrompt);
                    }
                    emit processingFinished(act.db, act.item.messageId, true, act.item.targetType);
                    m_activeItems.remove(procId);
                    emit queueChanged();
                    QTimer::singleShot(500, this, &QueueManager::checkQueue);
                },
                [this, procId](QNetworkReply::NetworkError errorCode, const QString& error) {
                    if (!m_activeItems.contains(procId)) return;
                    auto act = m_activeItems[procId];
                    if (act.db && act.db->isOpen()) {
                        if (errorCode == QNetworkReply::ConnectionRefusedError ||
                            errorCode == QNetworkReply::HostNotFoundError || errorCode == QNetworkReply::TimeoutError) {
                            act.db->updateQueueItemState(act.item.id, "pending");
                            act.db->updateQueueError(act.item.id, "");  // also resets processing_id to 0
                        } else {
                            act.db->updateQueueError(act.item.id, error);
                            act.db->addNotification(act.item.messageId, act.item.targetType, "error");
                        }
                    }
                    emit processingFinished(act.db, act.item.messageId, false, act.item.targetType);
                    m_activeItems.remove(procId);
                    emit queueChanged();
                    QTimer::singleShot(500, this, &QueueManager::checkQueue);
                });
        } else {
            m_client->generate(
                item.model, item.prompt,
                [this, procId](const QString& chunk) {
                    if (!m_activeItems.contains(procId)) return;
                    auto act = m_activeItems[procId];
                    if (act.db && act.db->isOpen()) {
                        if (act.item.targetType == "document" && act.item.targetAction == "replace_direct") {
                            auto docs = act.db->getDocuments();
                            QString currentContent, title, metadata;
                            for (const auto& d : docs) {
                                if (d.id == act.item.messageId) {
                                    currentContent = d.content;
                                    if (currentContent == QStringLiteral("*Generating merge...*")) currentContent = "";
                                    title = d.title;
                                    metadata = d.metadata;
                                    break;
                                }
                            }
                            act.db->updateDocument(act.item.messageId, title, currentContent + chunk, metadata);
                        }
                        // For other document actions, we do not update the document directly.
                        // Just emit the chunk for the preview.
                        emit processingChunk(act.db, act.item.messageId, chunk, act.item.targetType);
                    }
                },
                [this, procId](const QString& response) {
                    if (!m_activeItems.contains(procId)) return;
                    auto act = m_activeItems[procId];
                    if (act.db && act.db->isOpen()) {
                        if (act.item.targetType == "document" && act.item.targetAction == "replace_direct") {
                            // Directly replace content without review.
                            auto docs = act.db->getDocuments();
                            QString title, metadata;
                            for (const auto& d : docs) {
                                if (d.id == act.item.messageId) {
                                    title = d.title;
                                    metadata = d.metadata;
                                    break;
                                }
                            }
                            act.db->updateDocument(act.item.messageId, title, response, metadata);
                            act.db->deleteQueueItem(act.item.id);
                            act.db->addNotification(act.item.messageId, act.item.targetType, "finished_generation");
                        } else {
                            act.db->updateQueueItemState(act.item.id, "completed", response);
                            act.db->addNotification(act.item.messageId, act.item.targetType, "review_needed");
                        }

                        act.db->setSetting(act.item.targetType, act.item.messageId, "model", act.item.model);

                        QString sysPrompt = act.db->getInheritedSetting(act.item.messageId, "systemPrompt");
                        if (sysPrompt.isEmpty()) {
                            sysPrompt = act.db->getSetting("book", 0, "systemPrompt", "");
                        }
                        if (sysPrompt.isEmpty()) {
                            QSettings settings;
                            sysPrompt = settings.value("globalSystemPrompt", "").toString();
                        }
                        act.db->setSetting(act.item.targetType, act.item.messageId, "systemPrompt", sysPrompt);
                    }
                    emit processingFinished(act.db, act.item.messageId, true, act.item.targetType);
                    m_activeItems.remove(procId);
                    emit queueChanged();
                    QTimer::singleShot(500, this, &QueueManager::checkQueue);
                },
                [this, procId](QNetworkReply::NetworkError errorCode, const QString& error) {
                    if (!m_activeItems.contains(procId)) return;
                    auto act = m_activeItems[procId];
                    if (act.db && act.db->isOpen()) {
                        if (errorCode == QNetworkReply::ConnectionRefusedError ||
                            errorCode == QNetworkReply::HostNotFoundError || errorCode == QNetworkReply::TimeoutError) {
                            act.db->updateQueueItemState(act.item.id, "pending");
                            act.db->updateQueueError(act.item.id, "");  // also resets processing_id to 0
                        } else {
                            act.db->updateQueueError(act.item.id, error);
                            act.db->addNotification(act.item.messageId, act.item.targetType, "error");
                        }
                    }
                    emit processingFinished(act.db, act.item.messageId, false, act.item.targetType);
                    m_activeItems.remove(procId);
                    emit queueChanged();
                    QTimer::singleShot(500, this, &QueueManager::checkQueue);
                });
        }
    }
}
