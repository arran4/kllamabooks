#include <sqlcipher/sqlite3.h>

#include <QtTest>

#include "../src/db/Database.h"
#include "../src/db/MigrationFactory.h"
#include "../src/store/ArtifactStore.h"
#include "../src/store/ChatStore.h"
#include "../src/store/PromptStore.h"
#include "../src/store/RecoveryStore.h"
#include "../src/store/RunStore.h"

class TestChatStore : public QObject {
    Q_OBJECT

   private slots:
    void testRunProvenanceWithoutBlankPlaceholders();
    void testCancelFailureRetryAndMultiModelBranching();
    void testCopyMessageSubtreeAcrossBooks();
    void testMergeRerunExactSnapshotVsLatestSources();
    void testMigration29To30BackfillsChatsAndQuarantinesOrphans();
};

void TestChatStore::testRunProvenanceWithoutBlankPlaceholders() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(database, &err), qPrintable(err));

    store::ChatStore chatStore(database);
    auto chatRes = chatStore.createChat(0, "Architecture Chat", "Be concise.", "llama3");
    QVERIFY(chatRes.isSuccess());
    const int chatId = chatRes.value->id;

    auto userMsgRes = chatStore.addMessage(chatId, 0, "user", "Explain Storage vNext");
    QVERIFY(userMsgRes.isSuccess());
    const int userMsgId = *userMsgRes.value;

    // Enqueuing assistant reply must NOT create a blank assistant placeholder row
    auto runIdRes = chatStore.enqueueAssistantReply(userMsgId, "llama3", "Explain Storage vNext", 0);
    QVERIFY(runIdRes.isSuccess());
    QCOMPARE(chatStore.getMessagesForChat(chatId).size(), 1);

    // Completing the run creates the assistant message linked to runId and atomically marks the run applied
    auto replyMsgRes = chatStore.completeAssistantReply(*runIdRes.value, "Storage vNext uses versioned stores.");
    QVERIFY(replyMsgRes.isSuccess());

    store::RunStore runStore(database);
    auto runOpt = runStore.getRun(*runIdRes.value);
    QVERIFY(runOpt.has_value());
    QCOMPARE(runOpt->state, QStringLiteral("applied"));
    QCOMPARE(runOpt->resultVersionId, *replyMsgRes.value);

    const auto msgs = chatStore.getMessagesForChat(chatId);
    QCOMPARE(msgs.size(), 2);
    QCOMPARE(msgs.at(1).role, QStringLiteral("assistant"));
    QCOMPARE(msgs.at(1).runId, *runIdRes.value);
    QCOMPARE(msgs.at(1).parentId, userMsgId);

    sqlite3_close(rawDb);
}

void TestChatStore::testCopyMessageSubtreeAcrossBooks() {
    sqlite3* srcRaw = nullptr;
    sqlite3* dstRaw = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &srcRaw), SQLITE_OK);
    QCOMPARE(sqlite3_open(":memory:", &dstRaw), SQLITE_OK);
    db::Database srcDb(srcRaw);
    db::Database dstDb(dstRaw);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(srcDb, &err), qPrintable(err));
    QVERIFY2(runner.run(dstDb, &err), qPrintable(err));

    store::ChatStore srcStore(srcDb);
    auto chatRes = srcStore.createChat(0, "Source Chat");
    QVERIFY(chatRes.isSuccess());
    auto m1 = srcStore.addMessage(chatRes.value->id, 0, "user", "Root Q");
    auto m2 = srcStore.addMessage(chatRes.value->id, *m1.value, "assistant", "Root A");
    auto m3 = srcStore.addMessage(chatRes.value->id, *m2.value, "user", "Followup Q");
    QVERIFY(m1.isSuccess() && m2.isSuccess() && m3.isSuccess());

    auto copyRes = srcStore.copyMessageSubtree(*m1.value, dstDb, 0, 0);
    QVERIFY(copyRes.isSuccess());
    const int dstRootId = *copyRes.value;

    store::ChatStore dstStore(dstDb);
    auto dstChat = dstStore.getChatByRootMessage(dstRootId);
    QVERIFY(dstChat.has_value());
    QCOMPARE(dstChat->title, QStringLiteral("Source Chat"));

    const auto dstMsgs = dstStore.getMessagesForChat(dstChat->id);
    QCOMPARE(dstMsgs.size(), 3);
    QCOMPARE(dstMsgs.at(0).content, QStringLiteral("Root Q"));
    QCOMPARE(dstMsgs.at(1).parentId, dstMsgs.at(0).id);
    QCOMPARE(dstMsgs.at(2).parentId, dstMsgs.at(1).id);

    sqlite3_close(srcRaw);
    sqlite3_close(dstRaw);
}

void TestChatStore::testMergeRerunExactSnapshotVsLatestSources() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    db::MigrationRunner runner = db::MigrationFactory::createRunner();
    QString err;
    QVERIFY2(runner.run(database, &err), qPrintable(err));

    store::ArtifactStore artStore(database);
    auto a1 = artStore.createArtifact(store::ArtifactKind::Document, 0, "Doc 1", "V1", "");
    auto a2 = artStore.createArtifact(store::ArtifactKind::Document, 0, "Doc 2", "V1", "");
    QVERIFY(a1.isSuccess() && a2.isSuccess());

    store::PromptStore promptStore(database);
    auto rev = promptStore.createRevision("user", "", "merge", 0, "", "Merge docs", "llama3", true);
    QVERIFY(rev.isSuccess());

    store::RunStore runStore(database);
    QList<store::AiRunInput> inputs = {
        {0, 0, 0, a1.value->artifactId, a1.value->id, "source"},
        {0, 0, 1, a2.value->artifactId, a2.value->id, "source"},
    };
    auto mergeRun = runStore.enqueueRun(rev.value->id, "merge", 0, 0, "merge", "llama3", 0, std::nullopt, inputs);
    QVERIFY(mergeRun.isSuccess());

    // Advance Doc 1 to a new version
    QVERIFY(artStore.sealVersion(a1.value->id).isSuccess());
    auto a1v2 = artStore.createMutableDescendant(a1.value->id, "Doc 1", "V2", "");
    QVERIFY(a1v2.isSuccess());

    const auto exactInputs = runStore.resolveInputsForRerun(mergeRun.value->id, true);
    QCOMPARE(exactInputs.at(0).versionId, a1.value->id);

    const auto latestInputs = runStore.resolveInputsForRerun(mergeRun.value->id, false);
    QCOMPARE(latestInputs.at(0).versionId, a1v2.value->id);
    QCOMPARE(latestInputs.at(1).versionId, a2.value->id);

    sqlite3_close(rawDb);
}

void TestChatStore::testMigration29To30BackfillsChatsAndQuarantinesOrphans() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);

    db::MigrationRunner fullRunner = db::MigrationFactory::createRunner();
    db::MigrationRunner runner29;
    for (const auto& m : fullRunner.getMigrations()) {
        if (m.toVersion <= 29) runner29.addMigration(m);
    }
    QString err;
    QVERIFY2(runner29.run(database, &err), qPrintable(err));

    // Seed valid chat tree + legacy chats metadata + an orphaned message (parent_id=9999)
    QVERIFY(database.execute(
        "INSERT INTO messages (id, parent_id, folder_id, role, content) VALUES (1, 0, 0, 'user', 'Hello');"));
    QVERIFY(database.execute(
        "INSERT INTO messages (id, parent_id, folder_id, role, content) VALUES (2, 1, 0, 'assistant', 'Hi');"));
    QVERIFY(database.execute("INSERT INTO chats (message_id, title, model) VALUES (1, 'My Legacy Chat', 'llama3');"));
    QVERIFY(
        database.execute("INSERT INTO messages (id, parent_id, folder_id, role, content) VALUES (50, 9999, 0, 'user', "
                         "'Orphaned msg');"));

    QVERIFY2(fullRunner.run(database, &err), qPrintable(err));

    store::ChatStore chatStore(database);
    auto chat1 = chatStore.getChatByRootMessage(1);
    QVERIFY(chat1.has_value());
    QCOMPARE(chat1->title, QStringLiteral("My Legacy Chat"));
    QCOMPARE(chatStore.getMessagesForChat(chat1->id).size(), 2);

    // Orphaned message 50 must be recovered to parent_id = 0 and quarantined
    auto orphanMsg = chatStore.getMessage(50);
    QVERIFY(orphanMsg.has_value());
    QCOMPARE(orphanMsg->parentId, 0);

    store::RecoveryStore recoveryStore(database);
    QVERIFY(recoveryStore.getRecords(QString(), "messages").size() >= 1);

    sqlite3_close(rawDb);
}

void TestChatStore::testCancelFailureRetryAndMultiModelBranching() {
    sqlite3* rawDb = nullptr;
    QCOMPARE(sqlite3_open(":memory:", &rawDb), SQLITE_OK);
    db::Database database(rawDb);
    QString err;
    QVERIFY2(db::MigrationFactory::createRunner().run(database, &err), qPrintable(err));

    store::ChatStore chatStore(database);
    store::RunStore runStore(database);

    auto chatRes = chatStore.createChat(0, "Branching Chat", "", "llama3");
    QVERIFY(chatRes.isSuccess());
    auto userMsg = chatStore.addMessage(chatRes.value->id, 0, "user", "Compare two models");
    QVERIFY(userMsg.isSuccess());

    // Enqueue multi-model runs targeting the user message (no blank assistant rows)
    auto runA = chatStore.enqueueAssistantReply(*userMsg.value, "llama3", "Compare two models");
    auto runB = chatStore.enqueueAssistantReply(*userMsg.value, "mistral", "Compare two models");
    QVERIFY(runA.isSuccess() && runB.isSuccess());
    QCOMPARE(chatStore.getMessagesForChat(chatRes.value->id).size(), 1);

    // Cancel runA and fail runB -> still zero blank assistant rows in messages
    QVERIFY(runStore.claimRun(*runA.value, "w1").isSuccess());
    QVERIFY(runStore.cancelRun(*runA.value).isSuccess());
    QVERIFY(runStore.claimRun(*runB.value, "w1").isSuccess());
    QVERIFY(runStore.failRun(*runB.value, "Timeout").isSuccess());
    QCOMPARE(chatStore.getMessagesForChat(chatRes.value->id).size(), 1);
    QCOMPARE(runStore.getRun(*runA.value)->state, QStringLiteral("cancelled"));
    QCOMPARE(runStore.getRun(*runB.value)->state, QStringLiteral("failed"));
    QCOMPARE(runStore.getRun(*runB.value)->lastError, QStringLiteral("Timeout"));

    // Retry runB as a descendant run and enqueue a second model branch, then complete both
    auto retryB = runStore.createDescendantRun(*runB.value);
    QVERIFY(retryB.isSuccess());
    QCOMPARE(retryB.value->parentRunId, std::optional<int>(*runB.value));
    QVERIFY(runStore.claimRun(retryB.value->id, "w1").isSuccess());
    auto replyB = chatStore.completeAssistantReply(retryB.value->id, "Mistral answer");
    QVERIFY(replyB.isSuccess());

    auto runC = chatStore.enqueueAssistantReply(*userMsg.value, "qwen2", "Compare two models");
    QVERIFY(runC.isSuccess());
    QVERIFY(runStore.claimRun(*runC.value, "w1").isSuccess());
    auto replyC = chatStore.completeAssistantReply(*runC.value, "Qwen answer");
    QVERIFY(replyC.isSuccess());

    // Both completed replies are sibling assistant branches under userMsg with distinct run provenance
    const QList<store::ChatMessage> msgs = chatStore.getMessagesForChat(chatRes.value->id);
    QCOMPARE(msgs.size(), 3);
    QCOMPARE(msgs[1].parentId, *userMsg.value);
    QCOMPARE(msgs[1].role, QStringLiteral("assistant"));
    QCOMPARE(msgs[1].runId, retryB.value->id);
    QCOMPARE(msgs[2].parentId, *userMsg.value);
    QCOMPARE(msgs[2].role, QStringLiteral("assistant"));
    QCOMPARE(msgs[2].runId, *runC.value);

    sqlite3_close(rawDb);
}

QTEST_MAIN(TestChatStore)
#include "test_ChatStore.moc"
