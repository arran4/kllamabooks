#include <QtTest>
#include "../src/BookDatabase.h"

class TestBookDatabase : public QObject {
    Q_OBJECT

private slots:
    void testAddDocumentHappyPath();
    void testAddDocumentWithParent();
    void testAddDocumentUnopenedDb();
};

void TestBookDatabase::testAddDocumentHappyPath() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    int folderId = db.addFolder(0, "Test Folder", "folder");
    QVERIFY(folderId > 0);

    QString title = "My Test Document";
    QString content = "# Hello World\nThis is test content.";
    QString metadata = "{\"test\": true}";
    int parentId = 0; // Root document

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

    // Add root document
    int rootId = db.addDocument(folderId, "Root Doc", "Root Content", 0, "{}");
    QVERIFY(rootId > 0);

    // Add child document
    int childId = db.addDocument(folderId, "Child Doc", "Child Content", rootId, "{}");
    QVERIFY(childId > 0);

    std::optional<DocumentNode> retrievedChild = db.getDocument(childId);
    QVERIFY(retrievedChild.has_value());
    QCOMPARE(retrievedChild->parentId, rootId);

    db.close();
}

void TestBookDatabase::testAddDocumentUnopenedDb() {
    BookDatabase db(":memory:");
    // DO NOT open the database

    int docId = db.addDocument(1, "Title", "Content", 0, "{}");
    QCOMPARE(docId, -1);
}

QTEST_MAIN(TestBookDatabase)
#include "test_BookDatabase.moc"
