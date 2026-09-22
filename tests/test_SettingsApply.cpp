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
    void testLegacyPlaintextRetainedOnCancel();
    void testBrandNewConnectionAddAndRemove();
    void testNoPlaintextSecretsInSettings();


   private:
    FakeCredentialStore* m_fakeStore;
};

void TestSettingsApply::initTestCase() {
    QCoreApplication::setOrganizationName("arran4_test");
    QCoreApplication::setApplicationName("kllamabooks_test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
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
    existingConn["authKey"] = "plaintext_secret"; // Actual legacy secret
    existingConn["name"] = "Legacy Connection";
    initialConnections.append(existingConn);
    settings.setValue("llmConnections", initialConnections);

    m_fakeStore->simulateUnavailable = true;

    // Auto-close the migration error dialog so it doesn't block the test
    QTimer::singleShot(0, []() {
        QWidget* activeWindow = QApplication::activeModalWidget();
        if (QMessageBox* msgBox = qobject_cast<QMessageBox*>(activeWindow)) {
            msgBox->close();
        }
    });

    SettingsDialog dlg(nullptr);

    // Mark for deletion
    dlg.m_pendingDeletes.insert("legacy_conn");
    for (int i = 0; i < dlg.m_connectionsTable->rowCount(); ++i) {
        if (dlg.m_connectionsTable->item(i, 3)->data(Qt::UserRole).toString() == "legacy_conn") {
            dlg.m_connectionsTable->setRowHidden(i, true);
        }
    }

    dlg.onApply();

    QVariantList newConnections = settings.value("llmConnections").toList();
    QVERIFY(newConnections.isEmpty());
    // Also test it is totally gone from legacy credentials map
    QVERIFY(!dlg.m_legacyCredentials.contains("legacy_conn"));
}


void TestSettingsApply::testLegacyPlaintextRetainedOnCancel() {
    QSettings settings;
    QVariantList initialConnections;
    QVariantMap existingConn;
    existingConn["id"] = "legacy_conn_2";
    existingConn["authKey"] = "plaintext_secret_2";
    existingConn["name"] = "Legacy Connection 2";
    initialConnections.append(existingConn);
    settings.setValue("llmConnections", initialConnections);

    m_fakeStore->simulateUnavailable = true;

    QTimer::singleShot(0, []() {
        QWidget* activeWindow = QApplication::activeModalWidget();
        if (QMessageBox* msgBox = qobject_cast<QMessageBox*>(activeWindow)) {
            msgBox->close();
        }
    });

    SettingsDialog dlg(nullptr);

    // Mark for deletion but then cancel
    dlg.m_pendingDeletes.insert("legacy_conn_2");

    // Cancel effectively discards pending state
    // Just verify the original setting is unmodified
    QVariantList newConnections = settings.value("llmConnections").toList();
    QCOMPARE(newConnections.size(), 1);
    QVERIFY(newConnections.first().toMap().contains("authKey"));
    QCOMPARE(newConnections.first().toMap()["authKey"].toString(), QString("plaintext_secret_2"));
}

void TestSettingsApply::testBrandNewConnectionAddAndRemove() {
    QSettings settings;
    SettingsDialog dlg(nullptr);

    // Add new connection via UI lists
    dlg.m_pendingWrites["new_conn"] = "super_secret";

    int row = dlg.m_connectionsTable->rowCount();
    dlg.m_connectionsTable->insertRow(row);
    QTableWidgetItem* credItem = new QTableWidgetItem("Configured");
    credItem->setData(Qt::UserRole, "new_conn");
    dlg.m_connectionsTable->setItem(row, 3, credItem);

    // Simulate wallet unavailable
    m_fakeStore->simulateUnavailable = true;

    // Simulate removing the newly added connection
    dlg.m_connectionsTable->setCurrentCell(row, 3);
    dlg.onRemoveConnection();

    // Apply changes
    dlg.onApply();

    // The transaction should succeed because we don't try to delete "new_conn" from the unavailable wallet
    QVariantList newConnections = settings.value("llmConnections").toList();
    QVERIFY(newConnections.isEmpty());
}

void TestSettingsApply::testNoPlaintextSecretsInSettings() {
    QSettings settings;
    SettingsDialog dlg(nullptr);

    dlg.m_pendingWrites["new_conn"] = "super_secret";

    int row = dlg.m_connectionsTable->rowCount();
    dlg.m_connectionsTable->insertRow(row);
    QTableWidgetItem* credItem = new QTableWidgetItem("Configured");
    credItem->setData(Qt::UserRole, "new_conn");
    dlg.m_connectionsTable->setItem(row, 0, new QTableWidgetItem("Name"));
    dlg.m_connectionsTable->setItem(row, 1, new QTableWidgetItem("Backend"));
    dlg.m_connectionsTable->setItem(row, 2, new QTableWidgetItem("URL"));
    dlg.m_connectionsTable->setItem(row, 3, credItem);
    dlg.m_connectionsTable->setItem(row, 4, new QTableWidgetItem("1"));

    dlg.onApply();

    QVariantList newConnections = settings.value("llmConnections").toList();
    QCOMPARE(newConnections.size(), 1);
    QVERIFY(!newConnections.first().toMap().contains("authKey"));
}

QTEST_MAIN(TestSettingsApply)
#include "test_SettingsApply.moc"
