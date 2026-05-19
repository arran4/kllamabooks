#include <QtTest>
#include <QCoreApplication>
#include "../src/QueueManager.h"
#include "../src/BookDatabase.h"
#include "../src/OllamaClient.h"

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

private:
    std::shared_ptr<BookDatabase> m_db;
    QString m_testDbPath;
};

void TestQueueManager::initTestCase() {
    QCoreApplication::setOrganizationName("arran4_test");
    QCoreApplication::setApplicationName("kllamabooks_test");
}

void TestQueueManager::cleanupTestCase() {
}

void TestQueueManager::init() {
    m_testDbPath = QDir::tempPath() + "/test_queue_manager.db";
    QFile::remove(m_testDbPath);
    m_db = std::make_shared<BookDatabase>(m_testDbPath);
    m_db->open("test_password");
    m_db->initSchema();
}

void TestQueueManager::cleanup() {
    QueueManager& qm = QueueManager::instance();
    if (m_db) {
        qm.removeDatabase(m_db);
        m_db->close();
    }
    qm.setClient(nullptr);
    qm.resumeQueue();
    qm.setMaxConcurrent(1); // Reset to default value

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

    OllamaClient client;
    qm.setClient(&client);

    qm.pauseQueue();
    m_db->enqueuePrompt(1, "test_model", "test_prompt");

    qm.checkQueue();

    QueueManager::QueueStats stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 1);
    QCOMPARE(stats.processing, 0); // No items should move to processing since queue is paused

    qm.resumeQueue();

    // Force synchronous processing before the event loop can process network errors.
    qm.checkQueue();

    stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 0);
    QCOMPARE(stats.processing, 1);
}

void TestQueueManager::testEndpointDownPreventsProcessing() {
    QueueManager& qm = QueueManager::instance();
    qm.addDatabase(m_db);

    OllamaClient client;
    qm.setClient(&client);
    qm.resumeQueue();

    // Simulate endpoint going down
    emit client.connectionStatusChanged(false);
    QVERIFY(!qm.isEndpointUp());

    m_db->enqueuePrompt(1, "test_model", "test_prompt");
    qm.checkQueue();

    QueueManager::QueueStats stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 1);
    QCOMPARE(stats.processing, 0); // No items should process since endpoint is down

    // Simulate endpoint coming up, checkQueue is called automatically via signal
    emit client.connectionStatusChanged(true);
    QVERIFY(qm.isEndpointUp());

    // Force synchronous processing before network errors revert the state.
    qm.checkQueue();

    stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 0);
    QCOMPARE(stats.processing, 1);
}

void TestQueueManager::testMaxConcurrentLimits() {
    QueueManager& qm = QueueManager::instance();
    qm.addDatabase(m_db);
    OllamaClient client;
    qm.setClient(&client);
    qm.resumeQueue();

    // Explicitly set endpoint to true to override any state leaked from previous tests
    emit client.connectionStatusChanged(true);

    qm.setMaxConcurrent(1);
    QCOMPARE(qm.maxConcurrent(), 1);

    // Enqueue more items than the concurrent limit
    m_db->enqueuePrompt(1, "test_model", "test_prompt_1");
    m_db->enqueuePrompt(2, "test_model", "test_prompt_2");

    QueueManager::QueueStats stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 2);
    QCOMPARE(stats.processing, 0);

    qm.checkQueue();

    // After checkQueue, one item should be processing and one pending.
    stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 1);
    QCOMPARE(stats.processing, 1);
}

QTEST_MAIN(TestQueueManager)
#include "test_QueueManager.moc"
