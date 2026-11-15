#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QtTest>

#include "../src/BookDatabase.h"

class TestBookDatabase : public QObject {
    Q_OBJECT

   private slots:
    void initTestCase();
    void testOpenInvalidPath();
    void testOpenInvalidPassword();
    void testOpenCorruptedDatabase();
    void testInitSchema();
    void testAddDocumentHappyPath();
    void testAddDocumentWithParent();
    void testAddDocumentUnopenedDb();
    void testChatRenamePreservesMessageContent();
    void testNestedForkChatRename();
    void testArtifactStoreProductionCutoverLifecycle();
};

void TestBookDatabase::initTestCase() {
    QCoreApplication::setOrganizationName("TestOrg");
    QCoreApplication::setApplicationName("TestApp");
}

void TestBookDatabase::testOpenInvalidPath() {
    BookDatabase db("/nonexistent_directory_for_test/test.db");
    QVERIFY(!db.open("password"));
}

void TestBookDatabase::testOpenInvalidPassword() {
    QString dbPath = QDir::temp().filePath("test_db_invalid_pwd.db");
    QFile::remove(dbPath);

    {
        BookDatabase db(dbPath);
        QVERIFY(db.open("correct_password"));
    }

    {
        BookDatabase db(dbPath);
        QVERIFY(!db.open("wrong_password"));
    }

    QFile::remove(dbPath);
}

void TestBookDatabase::testOpenCorruptedDatabase() {
    QString dbPath = QDir::temp().filePath("test_db_corrupted.db");
    QFile::remove(dbPath);

    QFile file(dbPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("This is a garbage text file that is not a valid SQLite database.");
    file.close();

    BookDatabase db(dbPath);
    QVERIFY(!db.open("any_password"));
    QVERIFY(!db.open(""));

    QFile::remove(dbPath);
}

void TestBookDatabase::testInitSchema() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    QString debugInfo = db.getDatabaseDebugInfo();
    QVERIFY(debugInfo.contains("Schema Version: 28"));
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

void TestBookDatabase::testChatRenamePreservesMessageContent() {
    BookDatabase db(":memory:");
    QVERIFY(db.open(""));

    int msgId = db.addMessage(0, "Original message content", "user");
    QVERIFY(msgId > 0);

    auto msgOpt = db.getMessage(msgId);
    QVERIFY(msgOpt.has_value());
    QCOMPARE(msgOpt->content, QString("Original message content"));

    // Simulate what happens on chat rename
    ChatNode chat = db.getChat(msgId);
    chat.title = "New Chat Title";
    QVERIFY(db.updateChat(chat));

    // Verify the message content is preserved
    msgOpt = db.getMessage(msgId);
    QVERIFY(msgOpt.has_value());
    QCOMPARE(msgOpt->content, QString("Original message content"));

    // Verify the title was updated
    QHash<int, QString> titles = db.getAllChatTitles();
    QCOMPARE(titles.value(msgId), QString("New Chat Title"));
}

void TestBookDatabase::testNestedForkChatRename() {
    BookDatabase db(":memory:");
    QVERIFY(db.open(""));

    int parentMsgId = db.addMessage(0, "Parent message", "user");
    int childMsgId = db.addMessage(parentMsgId, "Child fork message", "assistant");

    // Set initial parent title
    ChatNode parentChat = db.getChat(parentMsgId);
    parentChat.title = "Parent Title";
    QVERIFY(db.updateChat(parentChat));

    // Simulate what happens on nested fork rename
    ChatNode childChat = db.getChat(childMsgId);
    childChat.title = "Forked Title";
    QVERIFY(db.updateChat(childChat));

    // Verify message contents are preserved
    auto parentMsgOpt = db.getMessage(parentMsgId);
    QVERIFY(parentMsgOpt.has_value());
    QCOMPARE(parentMsgOpt->content, QString("Parent message"));

    auto childMsgOpt = db.getMessage(childMsgId);
    QVERIFY(childMsgOpt.has_value());
    QCOMPARE(childMsgOpt->content, QString("Child fork message"));

    // Verify the titles
    QHash<int, QString> titles = db.getAllChatTitles();
    QCOMPARE(titles.value(parentMsgId), QString("Parent Title"));
    QCOMPARE(titles.value(childMsgId), QString("Forked Title"));
}

void TestBookDatabase::testArtifactStoreProductionCutoverLifecycle() {
    BookDatabase db(":memory:");
    QVERIFY(db.open("testpassword"));

    int folder1 = db.addFolder(0, "Folder 1", "documents");
    int folder2 = db.addFolder(0, "Folder 2", "documents");
    QVERIFY(folder1 > 0);
    QVERIFY(folder2 > 0);

    // 1. Document create, update, version history, restore, move, rename, delete
    int docId = db.addDocument(folder1, "Initial Doc", "Version 1 content", 0, "{\"k\":1}");
    QVERIFY(docId > 0);
    int v1Id = db.getArtifactCurrentVersionId(docId, "document");
    QVERIFY(v1Id > 0);

    // Save an explicit history snapshot and update document
    int histId = db.addDocumentHistoryReturningId(docId, "manual_edit_pre", "Version 1 content");
    QVERIFY(histId > 0);
    QVERIFY(db.updateDocument(docId, "Updated Doc", "Version 2 content"));

    auto docOpt = db.getDocument(docId);
    QVERIFY(docOpt.has_value());
    QCOMPARE(docOpt->title, QString("Updated Doc"));
    QCOMPARE(docOpt->content, QString("Version 2 content"));
    QCOMPARE(docOpt->metadata, QString("{\"k\":1}"));

    auto history = db.getDocumentHistory(docId);
    QVERIFY(!history.isEmpty());
    QCOMPARE(history.first().content, QString("Version 1 content"));
    QCOMPARE(history.first().actionType, QString("manual_edit_pre"));

    // Create a draft forked from the document
    int draftId = db.addDraft(folder1, "Draft of Updated Doc", "Draft content", docId, "document");
    QVERIFY(draftId > 0);
    auto draftOpt = db.getDraft(draftId);
    QVERIFY(draftOpt.has_value());
    QCOMPARE(draftOpt->parentId, docId);
    QCOMPARE(draftOpt->targetType, QString("document"));
    QCOMPARE(draftOpt->content, QString("Draft content"));
    // Forked source content should match the document's content at the time the draft was created
    QCOMPARE(db.getDraftForkedSourceContent(draftId), QString("Version 2 content"));

    // Restore document to historical version 1
    QVERIFY(db.restoreDocumentHistoryVersion(docId, histId));
    docOpt = db.getDocument(docId);
    QVERIFY(docOpt.has_value());
    QCOMPARE(docOpt->content, QString("Version 1 content"));
    int vRestoredId = db.getArtifactCurrentVersionId(docId, "document");
    QVERIFY(vRestoredId != v1Id);

    // Rename & move document
    QVERIFY(db.updateDocumentTitle(docId, "Renamed Doc", "document"));
    QVERIFY(db.moveItem({store::ItemKind::Document, docId, "documents"}, folder2));
    docOpt = db.getDocument(docId);
    QVERIFY(docOpt.has_value());
    QCOMPARE(docOpt->title, QString("Renamed Doc"));
    QCOMPARE(docOpt->folderId, folder2);

    // 2. Note & Template CRUD through ArtifactStore
    int noteId = db.addNote(0, "My Note", "Initial note body");
    QVERIFY(noteId > 0);
    QVERIFY(db.updateNote(noteId, "My Note Renamed", "Updated note body"));
    auto noteOpt = db.getNote(noteId);
    QVERIFY(noteOpt.has_value());
    QCOMPARE(noteOpt->title, QString("My Note Renamed"));
    QCOMPARE(noteOpt->content, QString("Updated note body"));

    int tplId = db.addTemplate(0, "My Template", "Template body {context}");
    QVERIFY(tplId > 0);
    QVERIFY(db.updateTemplate(tplId, "My Template v2", "Updated template {context}"));
    auto tplOpt = db.getTemplate(tplId);
    QVERIFY(tplOpt.has_value());
    QCOMPARE(tplOpt->title, QString("My Template v2"));
    QCOMPARE(tplOpt->content, QString("Updated template {context}"));

    QVERIFY(db.deleteDraft(draftId));
    QVERIFY(!db.getDraft(draftId).has_value());
    QVERIFY(db.deleteNote(noteId));
    QVERIFY(!db.getNote(noteId).has_value());
    QVERIFY(db.deleteTemplate(tplId));
    QVERIFY(!db.getTemplate(tplId).has_value());
    QVERIFY(db.deleteDocument(docId));
    QVERIFY(!db.getDocument(docId).has_value());
}

QTEST_MAIN(TestBookDatabase)
#include "test_BookDatabase.moc"
