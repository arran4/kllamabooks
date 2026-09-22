#include <QtTest>

#include "FakeCredentialStore.h"

class TestCredentialStore : public QObject {
    Q_OBJECT

   private slots:
    void testSuccessFlow();
    void testMissingCredential();
    void testWriteFailure();
    void testReadFailure();
    void testDeleteFailure();
    void testWalletUnavailable();
};

void TestCredentialStore::testSuccessFlow() {
    FakeCredentialStore store;

    // Write
    QCOMPARE(store.writeCredential("conn-1", "secret123"), CredentialStore::Result::Success);
    QVERIFY(store.hasCredential("conn-1"));
    QCOMPARE(store.getCredential("conn-1"), QString("secret123"));

    // Read
    QString secretOut;
    QCOMPARE(store.readCredential("conn-1", secretOut), CredentialStore::Result::Success);
    QCOMPARE(secretOut, QString("secret123"));

    // Delete
    QCOMPARE(store.deleteCredential("conn-1"), CredentialStore::Result::Success);
    QVERIFY(!store.hasCredential("conn-1"));
}

void TestCredentialStore::testMissingCredential() {
    FakeCredentialStore store;

    QString secretOut;
    QCOMPARE(store.readCredential("unknown", secretOut), CredentialStore::Result::NotFound);
    QVERIFY(secretOut.isEmpty());
}

void TestCredentialStore::testWriteFailure() {
    FakeCredentialStore store;
    store.simulateWriteFailure = true;

    QCOMPARE(store.writeCredential("conn-1", "secret123"), CredentialStore::Result::WriteFailure);
    QVERIFY(!store.hasCredential("conn-1"));
}

void TestCredentialStore::testReadFailure() {
    FakeCredentialStore store;
    store.writeCredential("conn-1", "secret123");

    store.simulateReadFailure = true;

    QString secretOut;
    QCOMPARE(store.readCredential("conn-1", secretOut), CredentialStore::Result::ReadFailure);
}

void TestCredentialStore::testDeleteFailure() {
    FakeCredentialStore store;
    store.writeCredential("conn-1", "secret123");

    store.simulateDeleteFailure = true;

    QCOMPARE(store.deleteCredential("conn-1"), CredentialStore::Result::DeleteFailure);
    QVERIFY(store.hasCredential("conn-1"));
}

void TestCredentialStore::testWalletUnavailable() {
    FakeCredentialStore store;
    store.simulateUnavailable = true;

    QString secretOut;
    QCOMPARE(store.writeCredential("conn-1", "secret123"), CredentialStore::Result::WalletUnavailable);
    QCOMPARE(store.readCredential("conn-1", secretOut), CredentialStore::Result::WalletUnavailable);
    QCOMPARE(store.deleteCredential("conn-1"), CredentialStore::Result::WalletUnavailable);
}

QTEST_MAIN(TestCredentialStore)
#include "test_CredentialStore.moc"
