#include <QtTest>

#include "FakeCredentialStore.h"

class MockWallet : public IWallet {
   public:
    bool hasFolder(const QString& /*f*/) override { return !m_simulateMissingFolder; }
    bool createFolder(const QString& /*f*/) override { return !m_simulateCreateFolderFailure; }
    bool setFolder(const QString& /*f*/) override { return !m_simulateSetFolderFailure; }
    bool hasEntry(const QString& key) override { return m_store.contains(key); }
    int writePassword(const QString& key, const QString& value) override {
        if (m_simulateWriteFailure) return -1;
        m_store[key] = value;
        return 0;
    }
    int readPassword(const QString& key, QString& value) override {
        if (m_simulateReadFailure || !m_store.contains(key)) return -1;
        value = m_store[key];
        return 0;
    }
    int removeEntry(const QString& key) override {
        if (m_simulateDeleteFailure || !m_store.contains(key)) return -1;
        m_store.remove(key);
        return 0;
    }

    bool m_simulateMissingFolder = false;
    bool m_simulateCreateFolderFailure = false;
    bool m_simulateSetFolderFailure = false;
    bool m_simulateWriteFailure = false;
    bool m_simulateReadFailure = false;
    bool m_simulateDeleteFailure = false;

    QHash<QString, QString> m_store;
};

class MockWalletProvider : public IWalletProvider {
   public:
    IWallet* openWallet() override {
        if (m_simulateWalletUnavailable) return nullptr;
        // The consumer deletes the wallet, so return a new instance, copying state.
        MockWallet* w = new MockWallet();
        w->m_simulateMissingFolder = m_simulateMissingFolder;
        w->m_simulateCreateFolderFailure = m_simulateCreateFolderFailure;
        w->m_simulateSetFolderFailure = m_simulateSetFolderFailure;
        w->m_simulateWriteFailure = m_simulateWriteFailure;
        w->m_simulateReadFailure = m_simulateReadFailure;
        w->m_simulateDeleteFailure = m_simulateDeleteFailure;
        w->m_store = m_store;
        return w;
    }

    // Updates global store from closed wallet instances when testing sequence operations
    void updateStore(const QHash<QString, QString>& s) { m_store = s; }

    bool m_simulateWalletUnavailable = false;
    bool m_simulateMissingFolder = false;
    bool m_simulateCreateFolderFailure = false;
    bool m_simulateSetFolderFailure = false;
    bool m_simulateWriteFailure = false;
    bool m_simulateReadFailure = false;
    bool m_simulateDeleteFailure = false;

    QHash<QString, QString> m_store;
};

class TestCredentialStore : public QObject {
    Q_OBJECT

   private slots:
    void testSuccessFlow();
    void testMissingCredential();
    void testWriteFailure();
    void testReadFailure();
    void testDeleteFailure();
    void testWalletUnavailable();

    // Actual KWallet testing via seam
    void testKWalletSuccess();
    void testKWalletCreateFolderFailure();
    void testKWalletSetFolderFailure();
    void testKWalletMissingEntry();
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

void TestCredentialStore::testKWalletSuccess() {
    MockWalletProvider* provider = new MockWalletProvider();
    KWalletCredentialStore store(provider);

    QCOMPARE(store.writeCredential("conn-1", "secret123"), CredentialStore::Result::Success);

    // KWallet deletes the wallet after operation, so mock state must persist in provider
    // In actual tests, the mock state resets per call since it creates a `new MockWallet()` from the provider state.
    // For this simple test, we will pre-seed the mock provider.
    provider->m_store["conn-1"] = "secret123";

    QString secretOut;
    QCOMPARE(store.readCredential("conn-1", secretOut), CredentialStore::Result::Success);
    QCOMPARE(secretOut, QString("secret123"));

    QCOMPARE(store.deleteCredential("conn-1"), CredentialStore::Result::Success);
}

void TestCredentialStore::testKWalletCreateFolderFailure() {
    MockWalletProvider* provider = new MockWalletProvider();
    provider->m_simulateMissingFolder = true;
    provider->m_simulateCreateFolderFailure = true;
    KWalletCredentialStore store(provider);

    QCOMPARE(store.writeCredential("conn-1", "secret123"), CredentialStore::Result::WalletUnavailable);
}

void TestCredentialStore::testKWalletSetFolderFailure() {
    MockWalletProvider* provider = new MockWalletProvider();
    provider->m_simulateSetFolderFailure = true;
    KWalletCredentialStore store(provider);

    QString secretOut;
    // Missing folder path but set folder fails on existing check paths
    provider->m_simulateMissingFolder = false;
    provider->m_store["conn-1"] = "secret123";  // Seed entry

    QCOMPARE(store.writeCredential("conn-1", "secret123"), CredentialStore::Result::WalletUnavailable);
    QCOMPARE(store.readCredential("conn-1", secretOut), CredentialStore::Result::WalletUnavailable);
    QCOMPARE(store.deleteCredential("conn-1"), CredentialStore::Result::WalletUnavailable);
}

void TestCredentialStore::testKWalletMissingEntry() {
    MockWalletProvider* provider = new MockWalletProvider();
    KWalletCredentialStore store(provider);

    QString secretOut;
    QCOMPARE(store.readCredential("unknown", secretOut), CredentialStore::Result::NotFound);

    // Deleting a non-existent entry when the folder exists returns Success idempotently
    QCOMPARE(store.deleteCredential("unknown"), CredentialStore::Result::Success);
}

QTEST_MAIN(TestCredentialStore)
#include "test_CredentialStore.moc"
