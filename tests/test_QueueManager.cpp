#include <QCoreApplication>
#include <QUuid>
#include <QtTest>

#include "../src/BookDatabase.h"
#include "../src/OllamaClient.h"
#include "../src/QueueManager.h"

class FakeOllamaClient : public OllamaClient {
   public:
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

    stats = qm.getQueueStats();
    QCOMPARE(stats.pending, 1);
    QCOMPARE(stats.processing, 1);
}

QTEST_GUILESS_MAIN(TestQueueManager)
#include "test_QueueManager.moc"
