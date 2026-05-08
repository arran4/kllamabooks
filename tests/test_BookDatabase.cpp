#include <QtTest>
#include "../src/BookDatabase.h"
#include <QTemporaryFile>

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
        QVERIFY(db.initSchema());

        QString debugInfo = db.getDatabaseDebugInfo();
        QVERIFY(debugInfo.contains("messages"));
        QVERIFY(debugInfo.contains("documents"));
        QVERIFY(debugInfo.contains("folders"));
        QVERIFY(debugInfo.contains("settings"));
        QVERIFY(debugInfo.contains("chats"));

        db.close();
    }
};

QTEST_MAIN(TestBookDatabase)
#include "test_BookDatabase.moc"
