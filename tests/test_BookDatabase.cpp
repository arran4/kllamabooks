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
        QVERIFY(debugInfo.contains("- schema_version\n"));
        QVERIFY(debugInfo.contains("- messages\n"));
        QVERIFY(debugInfo.contains("- documents\n"));
        QVERIFY(debugInfo.contains("- templates\n"));
        QVERIFY(debugInfo.contains("- drafts\n"));
        QVERIFY(debugInfo.contains("- notes\n"));
        QVERIFY(debugInfo.contains("- folders\n"));
        QVERIFY(debugInfo.contains("- settings\n"));
        QVERIFY(debugInfo.contains("- queue\n"));
        QVERIFY(debugInfo.contains("- notifications\n"));
        QVERIFY(debugInfo.contains("- comments\n"));
        QVERIFY(debugInfo.contains("- document_history\n"));
        QVERIFY(debugInfo.contains("- document_merges\n"));
        QVERIFY(debugInfo.contains("- chats\n"));

        // Let's also check one of the table schemas contains columns we expect
        QVERIFY(debugInfo.contains("CREATE TABLE messages"));
        QVERIFY(debugInfo.contains("id INTEGER PRIMARY KEY"));
        QVERIFY(debugInfo.contains("parent_id INTEGER"));

        db.close();
    }
};

QTEST_MAIN(TestBookDatabase)
#include "test_BookDatabase.moc"
