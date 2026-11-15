#include "ChatStore.h"

#include <sqlcipher/sqlite3.h>

#include <QHash>
#include <QQueue>

#include "../db/Transaction.h"
#include "PromptStore.h"
#include "RunStore.h"

namespace store {

namespace {

QString colText(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return QString();
    const unsigned char* txt = sqlite3_column_text(stmt, idx);
    return txt ? QString::fromUtf8(reinterpret_cast<const char*>(txt)) : QString();
}

ChatSession readChatRow(sqlite3_stmt* stmt) {
    ChatSession cs;
    cs.id = sqlite3_column_int(stmt, 0);
    cs.rootMessageId = sqlite3_column_int(stmt, 1);
    cs.folderId = sqlite3_column_int(stmt, 2);
    cs.title = colText(stmt, 3);
    cs.systemPrompt = colText(stmt, 4);
    cs.model = colText(stmt, 5);
    cs.createdAt = colText(stmt, 6);
    return cs;
}

ChatMessage readMsgRow(sqlite3_stmt* stmt) {
    ChatMessage msg;
    msg.id = sqlite3_column_int(stmt, 0);
    msg.chatId = sqlite3_column_int(stmt, 1);
    msg.parentId = sqlite3_column_int(stmt, 2);
    msg.folderId = sqlite3_column_int(stmt, 3);
    msg.runId = sqlite3_column_int(stmt, 4);
    msg.role = colText(stmt, 5);
    msg.content = colText(stmt, 6);
    msg.timestamp = colText(stmt, 7);
    msg.isExpanded = sqlite3_column_int(stmt, 8) != 0;
    return msg;
}

}  // namespace

// ============================================================================
// Constructor
// ============================================================================

ChatStore::ChatStore(db::Database& db) : m_db(db) {}

// ============================================================================
// Public Methods
// ============================================================================

Result<ChatSession> ChatStore::createChat(int folderId, const QString& title, const QString& systemPrompt,
                                          const QString& model) {
    if (!m_db.handle()) return Result<ChatSession>::fail(TransitionError::DatabaseError, "Database not open");

    const char* sql =
        "INSERT INTO chat_sessions (root_message_id, folder_id, title, system_prompt, model) "
        "VALUES (0, ?, ?, ?, ?) RETURNING id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<ChatSession>::fail(TransitionError::DatabaseError,
                                         QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }

    const QByteArray tUtf8 = title.toUtf8();
    const QByteArray sUtf8 = systemPrompt.toUtf8();
    const QByteArray mUtf8 = model.toUtf8();
    sqlite3_bind_int(stmt, 1, folderId);
    sqlite3_bind_text(stmt, 2, tUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, mUtf8.constData(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<ChatSession>::fail(TransitionError::DatabaseError, err);
    }
    const int chatId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    auto chatOpt = getChat(chatId);
    if (!chatOpt.has_value()) {
        return Result<ChatSession>::fail(TransitionError::DatabaseError, "Failed to reload created chat");
    }
    return Result<ChatSession>::success(*chatOpt);
}

std::optional<ChatSession> ChatStore::getChat(int chatId) const {
    if (!m_db.handle() || chatId <= 0) return std::nullopt;

    const char* sql =
        "SELECT id, root_message_id, folder_id, title, system_prompt, model, created_at "
        "FROM chat_sessions WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_int(stmt, 1, chatId);
    std::optional<ChatSession> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out = readChatRow(stmt);
    }
    sqlite3_finalize(stmt);
    return out;
}

std::optional<ChatSession> ChatStore::getChatByRootMessage(int rootMessageId) const {
    if (!m_db.handle() || rootMessageId <= 0) return std::nullopt;

    const char* sql =
        "SELECT id, root_message_id, folder_id, title, system_prompt, model, created_at "
        "FROM chat_sessions WHERE root_message_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_int(stmt, 1, rootMessageId);
    std::optional<ChatSession> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out = readChatRow(stmt);
    }
    sqlite3_finalize(stmt);
    return out;
}

QList<ChatSession> ChatStore::listChats(std::optional<int> folderId) const {
    QList<ChatSession> list;
    if (!m_db.handle()) return list;

    const char* sql = folderId.has_value()
                          ? "SELECT id, root_message_id, folder_id, title, system_prompt, model, created_at "
                            "FROM chat_sessions WHERE folder_id = ? ORDER BY id DESC;"
                          : "SELECT id, root_message_id, folder_id, title, system_prompt, model, created_at "
                            "FROM chat_sessions ORDER BY id DESC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return list;
    if (folderId.has_value()) {
        sqlite3_bind_int(stmt, 1, *folderId);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        list.append(readChatRow(stmt));
    }
    sqlite3_finalize(stmt);
    return list;
}

Result<bool> ChatStore::renameChat(int chatId, const QString& newTitle) {
    if (!m_db.handle()) return Result<bool>::fail(TransitionError::DatabaseError, "Database not open");
    if (chatId <= 0 || newTitle.trimmed().isEmpty()) {
        return Result<bool>::fail(TransitionError::InvalidCrossReference, "Invalid chat ID or empty title");
    }

    const char* sql = "UPDATE chat_sessions SET title = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<bool>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    const QByteArray tUtf8 = newTitle.toUtf8();
    sqlite3_bind_text(stmt, 1, tUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, chatId);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<bool>::fail(TransitionError::DatabaseError, err);
    }
    const int changes = sqlite3_changes(m_db.handle());
    sqlite3_finalize(stmt);
    if (changes == 0) return Result<bool>::fail(TransitionError::NotFound, "Chat session not found");
    return Result<bool>::success(true);
}

Result<int> ChatStore::addMessage(int chatId, int parentId, const QString& role, const QString& content, int runId) {
    if (!m_db.handle()) return Result<int>::fail(TransitionError::DatabaseError, "Database not open");
    if (chatId <= 0) return Result<int>::fail(TransitionError::InvalidCrossReference, "chatId must be positive");

    auto chatOpt = getChat(chatId);
    if (!chatOpt.has_value()) return Result<int>::fail(TransitionError::NotFound, "Chat session not found");

    const char* sql =
        "INSERT INTO messages (chat_id, parent_id, folder_id, run_id, role, content) "
        "VALUES (?, ?, ?, ?, ?, ?) RETURNING id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return Result<int>::fail(TransitionError::DatabaseError, QString::fromUtf8(sqlite3_errmsg(m_db.handle())));
    }
    const QByteArray rUtf8 = role.toUtf8();
    const QByteArray cUtf8 = content.toUtf8();
    sqlite3_bind_int(stmt, 1, chatId);
    sqlite3_bind_int(stmt, 2, parentId);
    sqlite3_bind_int(stmt, 3, chatOpt->folderId);
    sqlite3_bind_int(stmt, 4, runId);
    sqlite3_bind_text(stmt, 5, rUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, cUtf8.constData(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        const QString err = QString::fromUtf8(sqlite3_errmsg(m_db.handle()));
        sqlite3_finalize(stmt);
        return Result<int>::fail(TransitionError::DatabaseError, err);
    }
    const int msgId = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    if (chatOpt->rootMessageId == 0 && parentId == 0) {
        m_db.execute(QString("UPDATE chat_sessions SET root_message_id = %1 WHERE id = %2;").arg(msgId).arg(chatId));
    }
    return Result<int>::success(msgId);
}

std::optional<ChatMessage> ChatStore::getMessage(int messageId) const {
    if (!m_db.handle() || messageId <= 0) return std::nullopt;

    const char* sql =
        "SELECT id, chat_id, parent_id, folder_id, run_id, role, content, timestamp, is_expanded "
        "FROM messages WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_int(stmt, 1, messageId);
    std::optional<ChatMessage> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out = readMsgRow(stmt);
    }
    sqlite3_finalize(stmt);
    return out;
}

QList<ChatMessage> ChatStore::getMessagesForChat(int chatId) const {
    QList<ChatMessage> list;
    if (!m_db.handle() || chatId <= 0) return list;

    const char* sql =
        "SELECT id, chat_id, parent_id, folder_id, run_id, role, content, timestamp, is_expanded "
        "FROM messages WHERE chat_id = ? ORDER BY id ASC;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db.handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) return list;
    sqlite3_bind_int(stmt, 1, chatId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        list.append(readMsgRow(stmt));
    }
    sqlite3_finalize(stmt);
    return list;
}

Result<int> ChatStore::enqueueAssistantReply(int userMessageId, const QString& model, const QString& prompt,
                                             int priority) {
    if (!m_db.handle()) return Result<int>::fail(TransitionError::DatabaseError, "Database not open");
    auto msgOpt = getMessage(userMessageId);
    if (!msgOpt.has_value()) {
        return Result<int>::fail(TransitionError::NotFound, "Parent user message not found");
    }

    PromptStore promptStore(m_db);
    auto revRes = promptStore.createRevision("user", "", "message", userMessageId, "", prompt, model, true);
    if (!revRes.isSuccess()) {
        return Result<int>::fail(revRes.error.value_or(TransitionError::DatabaseError), revRes.errorMessage);
    }

    RunStore runStore(m_db);
    auto runRes = runStore.enqueueRun(revRes.value->id, "message", userMessageId, 0, "chat_reply", model, priority);
    if (!runRes.isSuccess()) {
        return Result<int>::fail(runRes.error.value_or(TransitionError::DatabaseError), runRes.errorMessage);
    }
    return Result<int>::success(runRes.value->id);
}

Result<int> ChatStore::completeAssistantReply(int runId, const QString& assistantContent) {
    if (!m_db.handle()) return Result<int>::fail(TransitionError::DatabaseError, "Database not open");
    db::Transaction tx(m_db);
    if (tx.isFailed()) return Result<int>::fail(TransitionError::DatabaseError, "Failed to start transaction");

    RunStore runStore(m_db);
    auto runOpt = runStore.getRun(runId);
    if (!runOpt.has_value()) return Result<int>::fail(TransitionError::NotFound, "Run not found");

    auto parentMsgOpt = getMessage(runOpt->targetId);
    if (!parentMsgOpt.has_value()) {
        return Result<int>::fail(TransitionError::NotFound, "Target user message not found");
    }

    auto addRes =
        addMessage(parentMsgOpt->chatId, parentMsgOpt->id, QStringLiteral("assistant"), assistantContent, runId);
    if (!addRes.isSuccess()) return addRes;

    auto compRes = runStore.completeRun(runId, assistantContent, *addRes.value);
    if (!compRes.isSuccess()) {
        return Result<int>::fail(compRes.error.value_or(TransitionError::DatabaseError), compRes.errorMessage);
    }

    if (!tx.commit()) {
        return Result<int>::fail(TransitionError::DatabaseError, "Failed to commit assistant reply");
    }
    return addRes;
}

Result<int> ChatStore::copyMessageSubtree(int sourceRootMessageId, db::Database& targetDb, int targetParentId,
                                          int targetFolderId) const {
    if (!m_db.handle() || !targetDb.handle()) {
        return Result<int>::fail(TransitionError::DatabaseError, "Source or target database not open");
    }

    auto rootOpt = getMessage(sourceRootMessageId);
    if (!rootOpt.has_value()) {
        return Result<int>::fail(TransitionError::NotFound, "Source root message not found");
    }

    // Collect subtree nodes in BFS order from source DB
    QList<ChatMessage> subtree;
    QQueue<int> queue;
    queue.enqueue(sourceRootMessageId);
    while (!queue.isEmpty()) {
        const int currId = queue.dequeue();
        auto currOpt = getMessage(currId);
        if (!currOpt.has_value()) continue;
        subtree.append(*currOpt);

        sqlite3_stmt* childStmt = nullptr;
        if (sqlite3_prepare_v2(m_db.handle(), "SELECT id FROM messages WHERE parent_id = ? ORDER BY id ASC;", -1,
                               &childStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(childStmt, 1, currId);
            while (sqlite3_step(childStmt) == SQLITE_ROW) {
                queue.enqueue(sqlite3_column_int(childStmt, 0));
            }
            sqlite3_finalize(childStmt);
        }
    }

    if (!targetDb.execute("BEGIN IMMEDIATE;")) {
        return Result<int>::fail(TransitionError::DatabaseError, "Failed to start target transaction");
    }
    auto rollback = [&targetDb](TransitionError err, const QString& msg) {
        targetDb.execute("ROLLBACK;");
        return Result<int>::fail(err, msg);
    };

    ChatStore targetStore(targetDb);
    int targetChatId = 0;
    if (targetParentId > 0) {
        auto parentOpt = targetStore.getMessage(targetParentId);
        if (!parentOpt.has_value()) {
            return rollback(TransitionError::NotFound, "Target parent message not found");
        }
        targetChatId = parentOpt->chatId;
    } else {
        auto srcChatOpt = getChat(rootOpt->chatId);
        const QString title = srcChatOpt.has_value() ? srcChatOpt->title : QStringLiteral("Copied Chat");
        const QString sysP = srcChatOpt.has_value() ? srcChatOpt->systemPrompt : QString();
        const QString mod = srcChatOpt.has_value() ? srcChatOpt->model : QString();
        auto newChatRes = targetStore.createChat(targetFolderId, title, sysP, mod);
        if (!newChatRes.isSuccess()) {
            return rollback(newChatRes.error.value_or(TransitionError::DatabaseError), newChatRes.errorMessage);
        }
        targetChatId = newChatRes.value->id;
    }

    QHash<int, int> idMap;
    int newRootId = 0;
    for (const ChatMessage& node : subtree) {
        const int newParent = (node.id == sourceRootMessageId) ? targetParentId : idMap.value(node.parentId, 0);
        auto insRes = targetStore.addMessage(targetChatId, newParent, node.role, node.content, 0);
        if (!insRes.isSuccess()) {
            return rollback(insRes.error.value_or(TransitionError::DatabaseError), insRes.errorMessage);
        }
        idMap.insert(node.id, *insRes.value);
        if (node.id == sourceRootMessageId) {
            newRootId = *insRes.value;
        }
    }

    if (!targetDb.execute("COMMIT;")) {
        return rollback(TransitionError::DatabaseError, "Failed to commit subtree copy");
    }
    return Result<int>::success(newRootId);
}

}  // namespace store
