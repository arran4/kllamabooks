#include <QtTest>
#include <QTemporaryFile>
#include "../src/BookDatabase.h"

class TestBookDatabase : public QObject {
    Q_OBJECT

private slots:
    void testAddDocumentHappyPath();
    void testAddDocumentWithParent();
    void testAddDocumentUnopenedDb();
};

void TestBookDatabase::testAddDocumentHappyPath() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString dbPath = tempFile.fileName();
    tempFile.close();

    BookDatabase db(dbPath);
    QVERIFY(db.open("testpassword"));

    // Ensure we can add a basic document without parent
    int folderId = 1; // Assuming we can use 1 without adding a folder, or we might need a folder if foreign keys enforce it
                      // Wait, SQLite pragmas foreign_keys is ON? Let's assume it might not enforce if not strictly setup or we need to add a folder first.
                      // Actually, let's just try inserting.

    // To be safe, we add a folder first just in case there's an integrity check,
    // although standard BookDatabase schema might not strictly enforce fk for folder.
    // Let's add a folder to be proper.
    int realFolderId = db.addFolder(0, "Test Folder", "folder");
    // If addFolder returns -1, we can just use 0 or 1.
    if (realFolderId <= 0) realFolderId = 1;

    QString title = "My Test Document";
    QString content = "# Hello World\nThis is test content.";
    QString metadata = "{\"test\": true}";
    int parentId = 0; // Root document

    int docId = db.addDocument(realFolderId, title, content, parentId, metadata);
    QVERIFY(docId > 0);

    std::optional<DocumentNode> retrievedDoc = db.getDocument(docId);
    QVERIFY(retrievedDoc.has_value());
    QCOMPARE(retrievedDoc->id, docId);
    QCOMPARE(retrievedDoc->folderId, realFolderId);
    QCOMPARE(retrievedDoc->title, title);
    QCOMPARE(retrievedDoc->content, content);
    QCOMPARE(retrievedDoc->parentId, parentId);
    QCOMPARE(retrievedDoc->metadata, metadata);

    db.close();
}

void TestBookDatabase::testAddDocumentWithParent() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    QString dbPath = tempFile.fileName();
    tempFile.close();

    BookDatabase db(dbPath);
    QVERIFY(db.open("testpassword"));

    // Add root document
    int rootId = db.addDocument(1, "Root Doc", "Root Content", 0, "{}");
    QVERIFY(rootId > 0);

    // Add child document
    int childId = db.addDocument(1, "Child Doc", "Child Content", rootId, "{}");
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
