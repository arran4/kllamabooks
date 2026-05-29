#include <QCoreApplication>
#include <QtTest>

#include "../src/BookDatabase.h"

class TestBookDatabase : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase();
    void testInitSchema();
    void testAddDocumentHappyPath();
    void testAddDocumentWithParent();
    void testAddDocumentUnopenedDb();
};

void TestBookDatabase::initTestCase() {
    QCoreApplication::setOrganizationName("TestOrg");
    QCoreApplication::setApplicationName("TestApp");
}

void TestBookDatabase::testInitSchema() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    QString debugInfo = db.getDatabaseDebugInfo();
    QVERIFY(debugInfo.contains("Schema Version: 21"));
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
    QVERIFY(debugInfo.contains("- prompt_history\n"));
    QVERIFY(debugInfo.contains("- chats\n"));

    QVERIFY(debugInfo.contains("CREATE TABLE messages"));
    QVERIFY(debugInfo.contains("id INTEGER PRIMARY KEY"));
    QVERIFY(debugInfo.contains("parent_id INTEGER"));

    db.close();
}

void TestBookDatabase::testAddDocumentHappyPath() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    int folderId = db.addFolder(0, "Test Folder", "folder");
    QVERIFY(folderId > 0);

    QString title = "My Test Document";
    QString content = "# Hello World\nThis is test content.";
    QString metadata = "{\"test\": true}";
    int parentId = 0;

    int docId = db.addDocument(folderId, title, content, parentId, metadata);
    QVERIFY(docId > 0);

    std::optional<DocumentNode> retrievedDoc = db.getDocument(docId);
    QVERIFY(retrievedDoc.has_value());
    QCOMPARE(retrievedDoc->id, docId);
    QCOMPARE(retrievedDoc->folderId, folderId);
    QCOMPARE(retrievedDoc->title, title);
    QCOMPARE(retrievedDoc->content, content);
    QCOMPARE(retrievedDoc->parentId, parentId);
    QCOMPARE(retrievedDoc->metadata, metadata);

    db.close();
}

void TestBookDatabase::testAddDocumentWithParent() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    int folderId = db.addFolder(0, "Test Folder", "folder");
    QVERIFY(folderId > 0);

    int rootId = db.addDocument(folderId, "Root Doc", "Root Content", 0, "{}");
    QVERIFY(rootId > 0);

    int childId = db.addDocument(folderId, "Child Doc", "Child Content", rootId, "{}");
    QVERIFY(childId > 0);

    std::optional<DocumentNode> retrievedChild = db.getDocument(childId);
    QVERIFY(retrievedChild.has_value());
    QCOMPARE(retrievedChild->parentId, rootId);

    db.close();
}

void TestBookDatabase::testAddDocumentUnopenedDb() {
    BookDatabase db(":memory:");

    int docId = db.addDocument(1, "Title", "Content", 0, "{}");
    QCOMPARE(docId, -1);
}

QTEST_MAIN(TestBookDatabase)
#include "test_BookDatabase.moc"
