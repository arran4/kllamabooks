#include <QtTest>

#include "FakeCredentialStore.h"

struct SharedWalletState {
    bool simulateWalletUnavailable = false;
    bool simulateMissingFolder = false;
    bool simulateCreateFolderFailure = false;
    bool simulateSetFolderFailure = false;
    bool simulateWriteFailure = false;
    bool simulateReadFailure = false;
    bool simulateDeleteFailure = false;

    int hasEntryCount = 0;
    int writePasswordCount = 0;
    int readPasswordCount = 0;
    int removeEntryCount = 0;

    int walletOpenCount = 0;
    int walletClosedCount = 0;

    int createFolderCount = 0;
    int setFolderCount = 0;

    QHash<QString, QString> store;

    void resetCounters() {
        hasEntryCount = 0;
        writePasswordCount = 0;
        readPasswordCount = 0;
        removeEntryCount = 0;
        createFolderCount = 0;
        setFolderCount = 0;
    }
};

class MockWallet : public IWallet {
   public:
    explicit MockWallet(SharedWalletState* state) : m_state(state) { m_state->walletOpenCount++; }

    ~MockWallet() override { m_state->walletClosedCount++; }

    bool hasFolder(const QString& /*f*/) override { return !m_state->simulateMissingFolder; }
    bool createFolder(const QString& /*f*/) override {
        m_state->createFolderCount++;
        if (!m_state->simulateCreateFolderFailure) {
            m_state->simulateMissingFolder = false;  // Cohorent state update
            return true;
        }
        return false;
    }
    bool setFolder(const QString& /*f*/) override {
        m_state->setFolderCount++;
        return !m_state->simulateSetFolderFailure;
    }
    bool hasEntry(const QString& key) override {
        m_state->hasEntryCount++;
        return m_state->store.contains(key);
    }
    int writePassword(const QString& key, const QString& value) override {
        m_state->writePasswordCount++;
        if (m_state->simulateWriteFailure) return -1;
        m_state->store[key] = value;
        return 0;
    }
    int readPassword(const QString& key, QString& value) override {
        m_state->readPasswordCount++;
        if (m_state->simulateReadFailure || !m_state->store.contains(key)) return -1;
        value = m_state->store[key];
        return 0;
    }
    int removeEntry(const QString& key) override {
        m_state->removeEntryCount++;
        if (m_state->simulateDeleteFailure || !m_state->store.contains(key)) return -1;
        m_state->store.remove(key);
        return 0;
    }

    SharedWalletState* m_state;
};

class MockWalletProvider : public IWalletProvider {
   public:
    explicit MockWalletProvider(SharedWalletState* state) : m_state(state) {}

    IWallet* openWallet() override {
        if (m_state->simulateWalletUnavailable) return nullptr;
        return new MockWallet(m_state);
    }

    SharedWalletState* m_state;
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
    SharedWalletState state;
    MockWalletProvider* provider = new MockWalletProvider(&state);
    KWalletCredentialStore store(provider);  // takes ownership

    QCOMPARE(store.writeCredential("conn-1", "secret123"), CredentialStore::Result::Success);

    QString secretOut;
    QCOMPARE(store.readCredential("conn-1", secretOut), CredentialStore::Result::Success);
    QCOMPARE(secretOut, QString("secret123"));

    QCOMPARE(store.deleteCredential("conn-1"), CredentialStore::Result::Success);

    QCOMPARE(store.readCredential("conn-1", secretOut), CredentialStore::Result::NotFound);
    QCOMPARE(store.deleteCredential("conn-1"), CredentialStore::Result::Success);  // Idempotent
}

void TestCredentialStore::testKWalletCreateFolderFailure() {
    SharedWalletState state;
    state.simulateMissingFolder = true;
    state.simulateCreateFolderFailure = true;

    MockWalletProvider* provider = new MockWalletProvider(&state);
    KWalletCredentialStore store(provider);

    QCOMPARE(store.writeCredential("conn-1", "secret123"), CredentialStore::Result::WalletUnavailable);

    // Assert 0 counts for all inner logic
    QCOMPARE(state.hasEntryCount, 0);
    QCOMPARE(state.writePasswordCount, 0);
    QCOMPARE(state.readPasswordCount, 0);
    QCOMPARE(state.removeEntryCount, 0);

    // Seed data isn't modified
    QVERIFY(!state.store.contains("conn-1"));
    QCOMPARE(state.walletOpenCount, state.walletClosedCount);
}

void TestCredentialStore::testKWalletSetFolderFailure() {
    SharedWalletState state;
    state.simulateSetFolderFailure = true;

    MockWalletProvider* provider = new MockWalletProvider(&state);
    KWalletCredentialStore store(provider);

    QString secretOut;
    // 1. Existing folder fails on setFolder for write
    state.simulateMissingFolder = false;
    state.store["conn-1"] = "secret123";  // Seed entry

    QCOMPARE(store.writeCredential("conn-1", "secret456"), CredentialStore::Result::WalletUnavailable);
    QCOMPARE(state.hasEntryCount, 0);
    QCOMPARE(state.writePasswordCount, 0);
    QCOMPARE(state.readPasswordCount, 0);
    QCOMPARE(state.removeEntryCount, 0);
    QCOMPARE(state.store["conn-1"], QString("secret123"));  // Value unchanged
    QCOMPARE(state.walletOpenCount, state.walletClosedCount);

    state.resetCounters();

    // 2. Existing folder fails on setFolder for read
    QCOMPARE(store.readCredential("conn-1", secretOut), CredentialStore::Result::WalletUnavailable);
    QCOMPARE(state.hasEntryCount, 0);
    QCOMPARE(state.writePasswordCount, 0);
    QCOMPARE(state.readPasswordCount, 0);
    QCOMPARE(state.removeEntryCount, 0);
    QVERIFY(secretOut.isEmpty());  // Did not read it
    QCOMPARE(state.walletOpenCount, state.walletClosedCount);

    state.resetCounters();

    // 3. Existing folder fails on setFolder for delete
    QCOMPARE(store.deleteCredential("conn-1"), CredentialStore::Result::WalletUnavailable);
    QCOMPARE(state.hasEntryCount, 0);
    QCOMPARE(state.writePasswordCount, 0);
    QCOMPARE(state.readPasswordCount, 0);
    QCOMPARE(state.removeEntryCount, 0);
    QCOMPARE(state.store["conn-1"], QString("secret123"));  // Value un-deleted
    QCOMPARE(state.walletOpenCount, state.walletClosedCount);

    state.resetCounters();

    // 4. Missing folder successfully created, but then fails on setFolder for write
    state.simulateMissingFolder = true;
    state.simulateCreateFolderFailure = false;

    QCOMPARE(store.writeCredential("conn-2", "secret"), CredentialStore::Result::WalletUnavailable);

    QCOMPARE(state.createFolderCount, 1);  // Validates the creation actually succeeded before selecting failed
    QCOMPARE(state.setFolderCount, 1);

    QCOMPARE(state.hasEntryCount, 0);
    QCOMPARE(state.writePasswordCount, 0);
    QCOMPARE(state.readPasswordCount, 0);
    QCOMPARE(state.removeEntryCount, 0);
    QVERIFY(!state.store.contains("conn-2"));
    QCOMPARE(state.walletOpenCount, state.walletClosedCount);
}

void TestCredentialStore::testKWalletMissingEntry() {
    SharedWalletState state;
    MockWalletProvider* provider = new MockWalletProvider(&state);
    KWalletCredentialStore store(provider);

    QString secretOut;
    QCOMPARE(store.readCredential("unknown", secretOut), CredentialStore::Result::NotFound);

    // Deleting a non-existent entry when the folder exists returns Success idempotently
    QCOMPARE(store.deleteCredential("unknown"), CredentialStore::Result::Success);
}

QTEST_MAIN(TestCredentialStore)
#include "test_CredentialStore.moc"
