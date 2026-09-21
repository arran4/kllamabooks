#include <QtTest>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>
#include <QSettings>

#include "../src/AppCredentialManager.h"
#include "FakeCredentialStore.h"

class TestAppCredentialManager : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testSuccessFromStore();
    void testFallbackFromQSettings();
    void testFallbackIgnoredWhenStoreHasIt();
    void testFailurePropagates();
};

void TestAppCredentialManager::init() {
    QCoreApplication::setOrganizationName("arran4_test");
    QCoreApplication::setApplicationName("kllamabooks_test");
    QSettings settings;
    settings.clear();
}

void TestAppCredentialManager::cleanup() {
    QSettings settings;
    settings.clear();
}

void TestAppCredentialManager::testSuccessFromStore() {
    FakeCredentialStore store;
    store.writeCredential("conn-1", "secret123");

    QString secret;
    CredentialStore::Result res = AppCredentialManager::getCredential("conn-1", secret, &store);

    QCOMPARE(res, CredentialStore::Result::Success);
    QCOMPARE(secret, QString("secret123"));
}

void TestAppCredentialManager::testFallbackFromQSettings() {
    FakeCredentialStore store;
    store.simulateUnavailable = true; // Wallet is locked

    QSettings settings;
    QVariantList connections;
    QVariantMap conn1;
    conn1["id"] = "conn-fallback";
    conn1["hasCredential"] = false;
    conn1["authKey"] = "fallback_secret";
    connections.append(conn1);
    settings.setValue("llmConnections", connections);

    QString secret;
    CredentialStore::Result res = AppCredentialManager::getCredential("conn-fallback", secret, &store);

    QCOMPARE(res, CredentialStore::Result::Success);
    QCOMPARE(secret, QString("fallback_secret"));
}

void TestAppCredentialManager::testFallbackIgnoredWhenStoreHasIt() {
    FakeCredentialStore store;
    store.simulateUnavailable = true;

    QSettings settings;
    QVariantList connections;
    QVariantMap conn1;
    conn1["id"] = "conn-fallback";
    conn1["hasCredential"] = true; // Pretend it has credential
    conn1["authKey"] = "fallback_secret"; // Should be ignored because hasCredential is true
    connections.append(conn1);
    settings.setValue("llmConnections", connections);

    QString secret;
    CredentialStore::Result res = AppCredentialManager::getCredential("conn-fallback", secret, &store);

    // Fallback not used since hasCredential is true, original error preserved
    QCOMPARE(res, CredentialStore::Result::WalletUnavailable);
    QVERIFY(secret.isEmpty());
}

void TestAppCredentialManager::testFailurePropagates() {
    FakeCredentialStore store;
    store.simulateUnavailable = true;

    QString secret;
    CredentialStore::Result res = AppCredentialManager::getCredential("missing-conn", secret, &store);

    QCOMPARE(res, CredentialStore::Result::WalletUnavailable);
}

QTEST_MAIN(TestAppCredentialManager)
#include "test_AppCredentialManager.moc"
