#include <QtTest>
#include "../src/BookDatabase.h"

class TestBookDatabase : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("TestOrg");
        QCoreApplication::setApplicationName("TestApp");
    }

    void testInitSchema() {
        BookDatabase db(":memory:");
        QVERIFY(db.open("testpassword"));

        QString debugInfo = db.getDatabaseDebugInfo();
        QVERIFY(debugInfo.contains("Schema Version: 19"));
        QVERIFY(debugInfo.contains("schema_version"));
        QVERIFY(debugInfo.contains("messages"));
        QVERIFY(debugInfo.contains("documents"));
        QVERIFY(debugInfo.contains("templates"));
        QVERIFY(debugInfo.contains("drafts"));
        QVERIFY(debugInfo.contains("notes"));
        QVERIFY(debugInfo.contains("folders"));
        QVERIFY(debugInfo.contains("settings"));
        QVERIFY(debugInfo.contains("queue"));
        QVERIFY(debugInfo.contains("notifications"));
        QVERIFY(debugInfo.contains("comments"));
        QVERIFY(debugInfo.contains("document_history"));
        QVERIFY(debugInfo.contains("document_merges"));
        QVERIFY(debugInfo.contains("chats"));

        db.close();
    }
};

QTEST_MAIN(TestBookDatabase)
#include "test_BookDatabase.moc"
