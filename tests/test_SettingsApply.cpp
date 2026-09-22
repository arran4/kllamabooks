#include <QApplication>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QtTest>

#define private public
#include "../src/SettingsDialog.h"
#undef private

#include "../src/AppCredentialManager.h"
#include "FakeCredentialStore.h"

class TestSettingsApply : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testSuccessfulAddEditRemove();
    void testPartialFailureRollback();
    void testFailedApplyRetainsUIState();
    void testLegacyOnlyRemovalWalletUnavailable();

   private:
    FakeCredentialStore* m_fakeStore;
};

void TestSettingsApply::initTestCase() {
    QCoreApplication::setOrganizationName("arran4_test");
    QCoreApplication::setApplicationName("kllamabooks_test");
}

void TestSettingsApply::cleanupTestCase() {}

void TestSettingsApply::init() {
    m_fakeStore = new FakeCredentialStore();
    AppCredentialManager::setStoreFactory([this]() -> CredentialStore* { return m_fakeStore; });
    QSettings settings;
    settings.clear();
}

void TestSettingsApply::cleanup() {
    delete m_fakeStore;
    AppCredentialManager::setStoreFactory(nullptr);
}

void TestSettingsApply::testSuccessfulAddEditRemove() {
    QSettings settings;
    QVariantList initialConnections;

    QVariantMap existingConn;
    existingConn["id"] = "conn1";
    existingConn["name"] = "Existing Connection";
    existingConn["url"] = "http://localhost:11434";
    existingConn["hasCredential"] = true;
    initialConnections.append(existingConn);

    settings.setValue("llmConnections", initialConnections);
    m_fakeStore->writeCredential("conn1", "secret1");

    SettingsDialog dlg(nullptr);

    // Simulate adding a new connection
    dlg.m_pendingWrites["conn2"] = "secret2";

    // Simulate editing existing connection
    dlg.m_pendingWrites["conn1"] = "new_secret1";

    // Simulate deleting a third connection (doesn't exist in settings but just test delete logic)
    m_fakeStore->writeCredential("conn3", "secret3");
    dlg.m_pendingDeletes.insert("conn3");

    QStringList failedWrites;
    QStringList failedDeletes;
    bool rollbackFailed = false;

    bool result = dlg.commitChanges(failedWrites, failedDeletes, rollbackFailed);

    QVERIFY(result);
    QVERIFY(!rollbackFailed);

    QString readSecret;
    QCOMPARE(m_fakeStore->readCredential("conn1", readSecret), CredentialStore::Result::Success);
    QCOMPARE(readSecret, QString("new_secret1"));

    QCOMPARE(m_fakeStore->readCredential("conn2", readSecret), CredentialStore::Result::Success);
    QCOMPARE(readSecret, QString("secret2"));

    QCOMPARE(m_fakeStore->readCredential("conn3", readSecret), CredentialStore::Result::NotFound);
}

void TestSettingsApply::testPartialFailureRollback() {
    QSettings settings;
    settings.clear();

    m_fakeStore->writeCredential("conn1", "secret1");

    SettingsDialog dlg(nullptr);
    dlg.m_pendingWrites["conn1"] = "new_secret1";
    dlg.m_pendingWrites["conn2"] = "secret2";
    dlg.m_pendingDeletes.insert("conn3");

    // Setup failure on writing conn2
    m_fakeStore->writeCredential("conn3", "secret3");
    m_fakeStore->failWriteIds.insert("conn2");

    QStringList failedWrites;
    QStringList failedDeletes;
    bool rollbackFailed = false;

    bool result = dlg.commitChanges(failedWrites, failedDeletes, rollbackFailed);

    QVERIFY(!result);
    QVERIFY(!rollbackFailed);

    QString readSecret;
    // Rollback should restore conn1 to secret1
    QCOMPARE(m_fakeStore->readCredential("conn1", readSecret), CredentialStore::Result::Success);
    QCOMPARE(readSecret, QString("secret1"));

    // conn2 should not exist
    QCOMPARE(m_fakeStore->readCredential("conn2", readSecret), CredentialStore::Result::NotFound);

    // conn3 should be restored (undeleted)
    QCOMPARE(m_fakeStore->readCredential("conn3", readSecret), CredentialStore::Result::Success);
    QCOMPARE(readSecret, QString("secret3"));
}

void TestSettingsApply::testFailedApplyRetainsUIState() {
    QSettings settings;
    QVariantList initialConnections;
    QVariantMap existingConn;
    existingConn["id"] = "conn1";
    existingConn["hasCredential"] = true;
    initialConnections.append(existingConn);
    settings.setValue("llmConnections", initialConnections);

    m_fakeStore->writeCredential("conn1", "secret1");

    SettingsDialog dlg(nullptr);

    // Add row to table
    dlg.m_connectionsTable->setRowCount(1);
    QTableWidgetItem* item = new QTableWidgetItem();
    item->setData(Qt::UserRole, "conn1");
    dlg.m_connectionsTable->setItem(0, 3, item);
    dlg.m_connectionsTable->setRowHidden(0, true);

    dlg.m_pendingDeletes.insert("conn1");

    m_fakeStore->simulateDeleteFailure = true;

    QTimer::singleShot(0, []() {
        QWidget* activeWindow = QApplication::activeModalWidget();
        if (QMessageBox* msgBox = qobject_cast<QMessageBox*>(activeWindow)) {
            msgBox->close();
        }
    });

    dlg.onApply();

    // UI state should be preserved
    QVERIFY(dlg.m_pendingDeletes.contains("conn1"));
    QVERIFY(dlg.m_connectionsTable->isRowHidden(0));

    // Retry apply and succeed
    m_fakeStore->simulateDeleteFailure = false;

    dlg.onApply();

    // It should now be gone from the store
    QString secret;
    QCOMPARE(m_fakeStore->readCredential("conn1", secret), CredentialStore::Result::NotFound);

    // And gone from llmConnections
    QVariantList newConnections = settings.value("llmConnections").toList();
    QVERIFY(newConnections.isEmpty());
}

void TestSettingsApply::testLegacyOnlyRemovalWalletUnavailable() {
    QSettings settings;
    QVariantList initialConnections;
    QVariantMap existingConn;
    existingConn["id"] = "legacy_conn";
    existingConn["hasCredential"] = false;  // Plaintext secret is still in settings
    initialConnections.append(existingConn);
    settings.setValue("llmConnections", initialConnections);

    SettingsDialog dlg(nullptr);
    dlg.m_legacyCredentials["legacy_conn"] = "plaintext_secret";
    dlg.m_pendingDeletes.insert("legacy_conn");
    for (int i = 0; i < dlg.m_connectionsTable->rowCount(); ++i) {
        if (dlg.m_connectionsTable->item(i, 3)->data(Qt::UserRole).toString() == "legacy_conn") {
            dlg.m_connectionsTable->setRowHidden(i, true);
        }
    }

    m_fakeStore->simulateUnavailable = true;

    QStringList failedWrites;
    QStringList failedDeletes;
    bool rollbackFailed = false;

    // Close the success dialog? onApply doesn't show one on success, it just calls accept()
    dlg.onApply();

    // It should succeed because it ignores wallet failure for purely legacy deletions
    QVariantList newConnections = settings.value("llmConnections").toList();
    QVERIFY(newConnections.isEmpty());
    QVERIFY(!dlg.m_legacyCredentials.contains("legacy_conn"));
    // We expect the original wallet failure simulation to not block completion
    QVERIFY(m_fakeStore->simulateUnavailable);  // the flag was indeed set
}

QTEST_MAIN(TestSettingsApply)
#include "test_SettingsApply.moc"
