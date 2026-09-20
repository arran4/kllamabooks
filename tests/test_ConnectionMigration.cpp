#include <QtTest>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

#include "../src/ConnectionMigration.h"
#include "FakeCredentialStore.h"

class TestConnectionMigration : public QObject {
    Q_OBJECT

private slots:
    void testSuccessfulMigration();
    void testMigrationFailurePreservesAuthKey();
    void testIdempotentMigration();
    void testNoAuthKeyMigration();
};

void TestConnectionMigration::testSuccessfulMigration() {
    FakeCredentialStore store;
    QVariantList connections;
    QVariantMap conn1;
    conn1["name"] = "Ollama 1";
    conn1["authKey"] = "my_secret_key";
    conn1["url"] = "http://test";
    connections.append(conn1);

    QStringList errors;
    bool needsSave = ConnectionMigration::migrate(connections, store, errors);

    QVERIFY(needsSave);
    QVERIFY(errors.isEmpty());
    QCOMPARE(connections.size(), 1);

    QVariantMap migratedConn = connections[0].toMap();
    QVERIFY(migratedConn.contains("id"));
    QVERIFY(!migratedConn["id"].toString().isEmpty());
    QVERIFY(!migratedConn.contains("authKey"));
    QVERIFY(migratedConn.contains("hasCredential"));
    QVERIFY(migratedConn["hasCredential"].toBool() == true);

    QString secret;
    QCOMPARE(store.readCredential(migratedConn["id"].toString(), secret), CredentialStore::Result::Success);
    QCOMPARE(secret, QString("my_secret_key"));
}

void TestConnectionMigration::testMigrationFailurePreservesAuthKey() {
    FakeCredentialStore store;
    store.simulateWriteFailure = true;

    QVariantList connections;
    QVariantMap conn1;
    conn1["name"] = "Ollama 1";
    conn1["authKey"] = "my_secret_key";
    connections.append(conn1);

    QStringList errors;
    bool needsSave = ConnectionMigration::migrate(connections, store, errors);

    // UUID will still be generated, so needsSave might be true
    QVERIFY(needsSave);
    QCOMPARE(errors.size(), 1);

    QVariantMap migratedConn = connections[0].toMap();
    QVERIFY(migratedConn.contains("id"));
    QVERIFY(migratedConn.contains("authKey")); // Auth key preserved
    QCOMPARE(migratedConn["authKey"].toString(), QString("my_secret_key"));
    QVERIFY(!migratedConn.contains("hasCredential"));

    QString secret;
    QCOMPARE(store.readCredential(migratedConn["id"].toString(), secret), CredentialStore::Result::NotFound);
}

void TestConnectionMigration::testIdempotentMigration() {
    FakeCredentialStore store;

    QVariantList connections;
    QVariantMap conn1;
    conn1["name"] = "Ollama 1";
    conn1["id"] = "test_uuid_1";
    conn1["hasCredential"] = true;
    connections.append(conn1);

    QStringList errors;
    bool needsSave = ConnectionMigration::migrate(connections, store, errors);

    QVERIFY(!needsSave); // No changes needed
    QVERIFY(errors.isEmpty());

    QVariantMap migratedConn = connections[0].toMap();
    QCOMPARE(migratedConn["id"].toString(), QString("test_uuid_1"));
    QVERIFY(!migratedConn.contains("authKey"));
    QVERIFY(migratedConn["hasCredential"].toBool() == true);
}

void TestConnectionMigration::testNoAuthKeyMigration() {
    FakeCredentialStore store;

    QVariantList connections;
    QVariantMap conn1;
    conn1["name"] = "Ollama 1";
    conn1["authKey"] = ""; // Empty explicitly
    connections.append(conn1);

    QStringList errors;
    bool needsSave = ConnectionMigration::migrate(connections, store, errors);

    QVERIFY(needsSave);
    QVERIFY(errors.isEmpty());

    QVariantMap migratedConn = connections[0].toMap();
    QVERIFY(migratedConn.contains("id"));
    QVERIFY(!migratedConn.contains("authKey"));
    QVERIFY(migratedConn.contains("hasCredential"));
    QVERIFY(migratedConn["hasCredential"].toBool() == false);
}

QTEST_MAIN(TestConnectionMigration)
#include "test_ConnectionMigration.moc"
