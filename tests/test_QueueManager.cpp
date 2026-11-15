#include <sqlite3.h>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTimer>
#include <QUuid>
#include <QtTest>

#include "../src/BookDatabase.h"
#include "../src/OllamaClient.h"
#include "../src/QueueManager.h"
#include "../src/db/Database.h"
#include "../src/store/ArtifactStore.h"
#include "../src/store/RunStore.h"

class FakeOllamaClient : public OllamaClient {
   public:
    std::function<void(const llm::LlmCallbacks&)> onGenerate;
    std::function<void(const llm::LlmCallbacks&)> onGenerateChat;

    QNetworkReply* generate(const llm::LlmRequestOptions&, const QString&,
                            const llm::LlmCallbacks& callbacks) override {
        if (onGenerate) {
            QTimer::singleShot(0, this, [cb = onGenerate, callbacks]() { cb(callbacks); });
        }
        return nullptr;
    }

    QNetworkReply* generateChat(const llm::LlmRequestOptions&, const QJsonArray&,
                                const llm::LlmCallbacks& callbacks) override {
        if (onGenerateChat) {
            QTimer::singleShot(0, this, [cb = onGenerateChat, callbacks]() { cb(callbacks); });
        }
        return nullptr;
    }

    QNetworkReply* generate(const QString& model, const QString& prompt, std::function<void(const QString&)> onChunk,
                            std::function<void(const QString&)> onComplete,
                            std::function<void(QNetworkReply::NetworkError, const QString&)> onError) override;

    QNetworkReply* generateChat(const QString& model, const QJsonArray& messages,
                                std::function<void(const QString&)> onChunk,
                                std::function<void(const QString&)> onComplete,
                                std::function<void(QNetworkReply::NetworkError, const QString&)> onError) override;
};

QNetworkReply* FakeOllamaClient::generate(const QString& model, const QString& prompt,
                                          std::function<void(const QString&)> onChunk,
                                          std::function<void(const QString&)> onComplete,
                                          std::function<void(QNetworkReply::NetworkError, const QString&)> onError) {
    Q_UNUSED(model)
    Q_UNUSED(prompt)
    Q_UNUSED(onChunk)
    Q_UNUSED(onComplete)
    Q_UNUSED(onError)
    return nullptr;
}

QNetworkReply* FakeOllamaClient::generateChat(
    const QString& model, const QJsonArray& messages, std::function<void(const QString&)> onChunk,
    std::function<void(const QString&)> onComplete,
    std::function<void(QNetworkReply::NetworkError, const QString&)> onError) {
    Q_UNUSED(model)
    Q_UNUSED(messages)
    Q_UNUSED(onChunk)
    Q_UNUSED(onComplete)
    Q_UNUSED(onError)
    return nullptr;
}

class TestQueueManager : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testAddRemoveDatabase();
    void testPauseResume();
    void testGetQueueStats();
    void testCheckQueueIsPaused();
    void testEndpointDownPreventsProcessing();
    void testMaxConcurrentLimits();
    void testDocumentReplaceDirectAtomicApply();
    void testDocumentReplaceDirectConcurrentEditConflict();
    void testDocumentReplaceDirectWriteFailureRollsBack();

   private:
    std::shared_ptr<BookDatabase> m_db;
    QString m_testDbPath;
    FakeOllamaClient* m_client = nullptr;
};

void TestQueueManager::initTestCase() {
    QCoreApplication::setOrganizationName("arran4_test");
    QCoreApplication::setApplicationName("kllamabooks_test");
    m_client = new FakeOllamaClient();
}

void TestQueueManager::cleanupTestCase() { delete m_client; }

void TestQueueManager::init() {
    m_client->onGenerate = nullptr;
    m_client->onGenerateChat = nullptr;
    m_testDbPath = QDir::temp().filePath("test_queue_manager_" + QUuid::createUuid().toString(QUuid::Id128) + ".db");
    QFile::remove(m_testDbPath);
    m_db = std::make_shared<BookDatabase>(m_testDbPath);
    QVERIFY(m_db->open("test_password"));
}

void TestQueueManager::cleanup() {
    QTest::qWait(200);

    QueueManager& qm = QueueManager::instance();
    if (m_db) {
        qm.removeDatabase(m_db);
        m_db->close();
    }
    qm.setClient(nullptr);
    qm.resumeQueue();
    qm.setMaxConcurrent(1);

    QFile::remove(m_testDbPath);
}

void TestQueueManager::testAddRemoveDatabase() {
    QueueManager& qm = QueueManager::instance();
    int initialCount = qm.databases().count();

    qm.addDatabase(m_db);
    QCOMPARE(qm.databases().count(), initialCount + 1);
    QVERIFY(qm.databases().contains(m_db));

    qm.removeDatabase(m_db);
    QCOMPARE(qm.databases().count(), initialCount);
    QVERIFY(!qm.databases().contains(m_db));
}

void TestQueueManager::testPauseResume() {
    QueueManager& qm = QueueManager::instance();
    qm.pauseQueue();
    QVERIFY(qm.isPaused());

    qm.resumeQueue();
    QVERIFY(!qm.isPaused());
}

void TestQueueManager::testGetQueueStats() {
    QueueManager& qm = QueueManager::instance();
    qm.addDatabase(m_db);

    QueueManager::QueueStats stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 0);
    QCOMPARE(stats.processing, 0);
    QCOMPARE(stats.completed, 0);
    QCOMPARE(stats.error, 0);

    m_db->enqueuePrompt(1, "test_model", "test_prompt");

    stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 1);
}

void TestQueueManager::testCheckQueueIsPaused() {
    QueueManager& qm = QueueManager::instance();
    qm.addDatabase(m_db);
    qm.setClient(m_client);
    emit m_client->connectionStatusChanged(true);

    qm.pauseQueue();
    m_db->enqueuePrompt(1, "test_model", "test_prompt");

    qm.checkQueue();

    QueueManager::QueueStats stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 1);
    QCOMPARE(stats.processing, 0);

    qm.resumeQueue();
    qm.checkQueue();

    stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 0);
    QCOMPARE(stats.processing, 1);
}

void TestQueueManager::testEndpointDownPreventsProcessing() {
    QueueManager& qm = QueueManager::instance();
    qm.addDatabase(m_db);
    qm.setClient(m_client);
    qm.resumeQueue();

    emit m_client->connectionStatusChanged(false);
    QVERIFY(!qm.isEndpointUp());

    m_db->enqueuePrompt(1, "test_model", "test_prompt");
    qm.checkQueue();

    QueueManager::QueueStats stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 1);
    QCOMPARE(stats.processing, 0);

    emit m_client->connectionStatusChanged(true);
    QVERIFY(qm.isEndpointUp());

    qm.checkQueue();

    stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 0);
    QCOMPARE(stats.processing, 1);
}

void TestQueueManager::testMaxConcurrentLimits() {
    QueueManager& qm = QueueManager::instance();
    qm.addDatabase(m_db);
    qm.setClient(m_client);
    emit m_client->connectionStatusChanged(true);
    qm.resumeQueue();

    qm.setMaxConcurrent(1);
    QCOMPARE(qm.maxConcurrent(), 1);

    m_db->enqueuePrompt(1, "test_model", "test_prompt_1");
    m_db->enqueuePrompt(2, "test_model", "test_prompt_2");

    QueueManager::QueueStats stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 2);
    QCOMPARE(stats.processing, 0);

    qm.checkQueue();
    QTest::qWait(200);

    stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 1);
    QCOMPARE(stats.processing, 1);
}

void TestQueueManager::testDocumentReplaceDirectAtomicApply() {
    QueueManager& qm = QueueManager::instance();
    qm.addDatabase(m_db);
    qm.setClient(m_client);
    qm.setMaxConcurrent(2);
    emit m_client->connectionStatusChanged(true);
    qm.resumeQueue();

    const int docId = m_db->addDocument(0, "Original Title", "Original Body", 0, "{}");
    QVERIFY(docId > 0);
    const int runId = m_db->enqueuePrompt(docId, "test_model", "Rewrite", 0, "document", 0, "replace_direct");
    QVERIFY(runId > 0);

    db::Database rawDb(reinterpret_cast<sqlite3*>(m_db->getDatabaseHandleForTesting()));
    store::RunStore runStore(rawDb);
    store::ArtifactStore artStore(rawDb);
    auto queuedRun = runStore.getRun(runId);
    QVERIFY(queuedRun.has_value());
    QVERIFY(queuedRun->baseVersionId > 0);
    auto baseVer = artStore.getVersion(queuedRun->baseVersionId);
    QVERIFY(baseVer.has_value());
    QVERIFY(baseVer->isSealed);

    m_client->onGenerate = [](const llm::LlmCallbacks& cb) { cb.onComplete("Generated Body"); };
    QSignalSpy finishedSpy(&qm, &QueueManager::processingFinished);

    qm.checkQueue();
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(2).toBool(), true);
    QCOMPARE(finishedSpy.at(0).at(3).toString(), QStringLiteral("document"));

    auto docOpt = m_db->getDocument(docId);
    QVERIFY(docOpt.has_value());
    QCOMPARE(docOpt->content, QStringLiteral("Generated Body"));

    auto appliedRun = runStore.getRun(runId);
    QVERIFY(appliedRun.has_value());
    QCOMPARE(appliedRun->state, QStringLiteral("applied"));
    QVERIFY(appliedRun->resultVersionId > queuedRun->baseVersionId);
    QCOMPARE(appliedRun->responseText, QStringLiteral("Generated Body"));
}

void TestQueueManager::testDocumentReplaceDirectConcurrentEditConflict() {
    QueueManager& qm = QueueManager::instance();
    qm.addDatabase(m_db);
    qm.setClient(m_client);
    qm.setMaxConcurrent(2);
    emit m_client->connectionStatusChanged(true);
    qm.resumeQueue();

    const int docId = m_db->addDocument(0, "Title", "V1 Body", 0, "{}");
    QVERIFY(docId > 0);
    const int runId = m_db->enqueuePrompt(docId, "test_model", "Rewrite", 0, "document", 0, "replace_direct");
    QVERIFY(runId > 0);

    // Concurrent edit before the queued run finishes
    QVERIFY(m_db->updateDocument(docId, "Title", "User Concurrent Edit", "{}"));

    m_client->onGenerate = [](const llm::LlmCallbacks& cb) { cb.onComplete("Stale AI Body"); };
    QSignalSpy finishedSpy(&qm, &QueueManager::processingFinished);

    qm.checkQueue();
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(2).toBool(), false);
    QCOMPARE(finishedSpy.at(0).at(3).toString(), QStringLiteral("document"));

    auto docOpt = m_db->getDocument(docId);
    QVERIFY(docOpt.has_value());
    QCOMPARE(docOpt->content, QStringLiteral("User Concurrent Edit"));

    db::Database rawDb(reinterpret_cast<sqlite3*>(m_db->getDatabaseHandleForTesting()));
    store::RunStore runStore(rawDb);
    auto conflictRun = runStore.getRun(runId);
    QVERIFY(conflictRun.has_value());
    QCOMPARE(conflictRun->state, QStringLiteral("conflict"));
    QCOMPARE(conflictRun->responseText, QStringLiteral("Stale AI Body"));
}

void TestQueueManager::testDocumentReplaceDirectWriteFailureRollsBack() {
    QueueManager& qm = QueueManager::instance();
    qm.addDatabase(m_db);
    qm.setClient(m_client);
    qm.setMaxConcurrent(2);
    emit m_client->connectionStatusChanged(true);
    qm.resumeQueue();

    const int docId = m_db->addDocument(0, "Title", "Original Safe Body", 0, "{}");
    QVERIFY(docId > 0);
    const int runId = m_db->enqueuePrompt(docId, "test_model", "Rewrite", 0, "document", 0, "replace_direct");
    QVERIFY(runId > 0);

    db::Database rawDb(reinterpret_cast<sqlite3*>(m_db->getDatabaseHandleForTesting()));
    QVERIFY(
        rawDb.execute("CREATE TRIGGER fail_apply_run BEFORE UPDATE ON ai_runs "
                      "WHEN NEW.state = 'applied' "
                      "BEGIN "
                      "  SELECT RAISE(ABORT, 'injected_apply_fail'); "
                      "END;"));

    m_client->onGenerate = [](const llm::LlmCallbacks& cb) { cb.onComplete("AI Body Should Rollback"); };
    QSignalSpy finishedSpy(&qm, &QueueManager::processingFinished);

    qm.checkQueue();
    QTRY_COMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(2).toBool(), false);
    QCOMPARE(finishedSpy.at(0).at(3).toString(), QStringLiteral("document"));

    auto docOpt = m_db->getDocument(docId);
    QVERIFY(docOpt.has_value());
    QCOMPARE(docOpt->content, QStringLiteral("Original Safe Body"));

    store::RunStore runStore(rawDb);
    auto failedApplyRun = runStore.getRun(runId);
    QVERIFY(failedApplyRun.has_value());
    QCOMPARE(failedApplyRun->state, QStringLiteral("completed"));
    QCOMPARE(failedApplyRun->responseText, QStringLiteral("AI Body Should Rollback"));
    QVERIFY(!failedApplyRun->lastError.isEmpty());
}

QTEST_GUILESS_MAIN(TestQueueManager)
#include "test_QueueManager.moc"
