#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "../db/Database.h"
#include "ArtifactStore.h"

namespace store {

struct ChatSession {
    int id = 0;
    int rootMessageId = 0;
    int folderId = 0;
    QString title;
    QString systemPrompt;
    QString model;
    QString createdAt;
};

struct ChatMessage {
    int id = 0;
    int chatId = 0;
    int parentId = 0;
    int folderId = 0;
    int runId = 0;
    QString role;
    QString content;
    QString timestamp;
    bool isExpanded = false;
};

class ChatStore {
   public:
    explicit ChatStore(db::Database& db);

    Result<ChatSession> createChat(int folderId, const QString& title, const QString& systemPrompt = QString(),
                                   const QString& model = QString());
    std::optional<ChatSession> getChat(int chatId) const;
    std::optional<ChatSession> getChatByRootMessage(int rootMessageId) const;
    QList<ChatSession> listChats(std::optional<int> folderId = std::nullopt) const;
    Result<bool> renameChat(int chatId, const QString& newTitle);

    Result<int> addMessage(int chatId, int parentId, const QString& role, const QString& content, int runId = 0);
    std::optional<ChatMessage> getMessage(int messageId) const;
    QList<ChatMessage> getMessagesForChat(int chatId) const;

    // Run-to-message provenance without blank assistant placeholder rows
    Result<int> enqueueAssistantReply(int userMessageId, const QString& model, const QString& prompt, int priority = 0);
    Result<int> completeAssistantReply(int runId, const QString& assistantContent);

    // Transactional subtree copy for branching and cross-book transfer
    Result<int> copyMessageSubtree(int sourceRootMessageId, db::Database& targetDb, int targetParentId = 0,
                                   int targetFolderId = 0) const;

   private:
    db::Database& m_db;
};

}  // namespace store
