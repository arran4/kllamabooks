#include "MigrationFactory.h"

namespace db {

MigrationRunner MigrationFactory::createRunner() {
    MigrationRunner runner;

    runner.addMigration({0, 1, "Initial Schema", [](Database& db) {
                             return db.execute(
                                 "CREATE TABLE IF NOT EXISTS messages ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                 "parent_id INTEGER, "
                                 "folder_id INTEGER DEFAULT 0, "
                                 "role TEXT, "
                                 "content TEXT, "
                                 "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
                                 ");"
                                 "CREATE TABLE IF NOT EXISTS documents ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                 "folder_id INTEGER DEFAULT 0, "
                                 "title TEXT, "
                                 "content TEXT, "
                                 "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
                                 ");"
                                 "CREATE TABLE IF NOT EXISTS templates ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                 "folder_id INTEGER DEFAULT 0, "
                                 "title TEXT, "
                                 "content TEXT, "
                                 "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
                                 ");"
                                 "CREATE TABLE IF NOT EXISTS drafts ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                 "folder_id INTEGER DEFAULT 0, "
                                 "title TEXT, "
                                 "content TEXT, "
                                 "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
                                 ");"
                                 "CREATE TABLE IF NOT EXISTS notes ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                 "folder_id INTEGER DEFAULT 0, "
                                 "title TEXT, "
                                 "content TEXT, "
                                 "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
                                 ");"
                                 "CREATE TABLE IF NOT EXISTS folders ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                 "parent_id INTEGER DEFAULT 0, "
                                 "name TEXT, "
                                 "type TEXT, "
                                 "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
                                 "position INTEGER DEFAULT 0"
                                 ");"
                                 "CREATE TABLE IF NOT EXISTS settings ("
                                 "scope TEXT, "
                                 "target_id INTEGER, "
                                 "key TEXT, "
                                 "value TEXT, "
                                 "PRIMARY KEY(scope, target_id, key)"
                                 ");");
                         }});

    runner.addMigration({1, 2, "Settings Table", [](Database& db) {
                             return db.execute(
                                 "CREATE TABLE IF NOT EXISTS settings ("
                                 "scope TEXT, target_id INTEGER, key TEXT, value TEXT, "
                                 "PRIMARY KEY(scope, target_id, key));");
                         }});

    runner.addMigration(
        {2, 3, "Folders, Templates, Drafts, Folder ID", [](Database& db) {
             bool ok = db.execute(
                 "CREATE TABLE IF NOT EXISTS templates (id INTEGER PRIMARY KEY AUTOINCREMENT, folder_id INTEGER "
                 "DEFAULT 0, title TEXT, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);");
             ok =
                 ok && db.execute(
                           "CREATE TABLE IF NOT EXISTS drafts (id INTEGER PRIMARY KEY AUTOINCREMENT, folder_id INTEGER "
                           "DEFAULT 0, title TEXT, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);");
             ok = ok && db.execute(
                            "CREATE TABLE IF NOT EXISTS folders (id INTEGER PRIMARY KEY AUTOINCREMENT, parent_id "
                            "INTEGER DEFAULT 0, name TEXT, type TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
                            "position INTEGER DEFAULT 0);");
             db.execute("ALTER TABLE documents ADD COLUMN folder_id INTEGER DEFAULT 0;");
             db.execute("ALTER TABLE notes ADD COLUMN folder_id INTEGER DEFAULT 0;");
             return ok;
         }});

    runner.addMigration({3, 4, "Messages Folder ID", [](Database& db) {
                             db.execute("ALTER TABLE messages ADD COLUMN folder_id INTEGER DEFAULT 0;");
                             return true;
                         }});

    runner.addMigration({4, 5, "Queue and Notifications", [](Database& db) {
                             bool ok = db.execute(
                                 "CREATE TABLE IF NOT EXISTS queue (id INTEGER PRIMARY KEY AUTOINCREMENT, message_id "
                                 "INTEGER, model TEXT, prompt TEXT, status TEXT, priority INTEGER DEFAULT 0, "
                                 "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
                             ok = ok && db.execute(
                                            "CREATE TABLE IF NOT EXISTS notifications (id INTEGER PRIMARY KEY "
                                            "AUTOINCREMENT, message_id INTEGER, type TEXT, is_dismissed BOOLEAN "
                                            "DEFAULT 0, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);");
                             return ok;
                         }});

    runner.addMigration({5, 6, "Clean Queue",
                         [](Database& db) { return db.execute("DELETE FROM queue WHERE status = 'completed';"); }});

    runner.addMigration({6, 7, "Comments", [](Database& db) {
                             return db.execute(
                                 "CREATE TABLE IF NOT EXISTS comments ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                 "entity_type TEXT, "
                                 "entity_id INTEGER, "
                                 "content TEXT, "
                                 "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                                 ");");
                         }});

    runner.addMigration({7, 8, "Documents Parent ID", [](Database& db) {
                             db.execute("ALTER TABLE documents ADD COLUMN parent_id INTEGER DEFAULT 0;");
                             return true;
                         }});

    runner.addMigration({8, 9, "Queue Target Type", [](Database& db) {
                             return db.execute("ALTER TABLE queue ADD COLUMN target_type TEXT DEFAULT 'message';");
                         }});

    runner.addMigration({9, 10, "Queue Processing PID", [](Database& db) {
                             return db.execute("ALTER TABLE queue ADD COLUMN processing_pid INTEGER DEFAULT 0;");
                         }});

    runner.addMigration({10, 11, "Queue Refactor", [](Database& db) {
                             bool ok = db.execute(
                                 "CREATE TABLE IF NOT EXISTS queue_new (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                 "message_id INTEGER, model TEXT, prompt TEXT, processing_id INTEGER DEFAULT 0, "
                                 "last_error TEXT DEFAULT '', priority INTEGER DEFAULT 0, created_at DATETIME DEFAULT "
                                 "CURRENT_TIMESTAMP, target_type TEXT DEFAULT 'message');");
                             ok = ok && db.execute(
                                            "INSERT INTO queue_new (id, message_id, model, prompt, processing_id, "
                                            "priority, created_at, target_type) SELECT id, message_id, model, prompt, "
                                            "processing_pid, priority, created_at, target_type FROM queue;");
                             ok = ok && db.execute("DROP TABLE queue;");
                             ok = ok && db.execute("ALTER TABLE queue_new RENAME TO queue;");
                             return ok;
                         }});

    runner.addMigration(
        {11, 12, "Chats Table", [](Database& db) {
             bool ok = db.execute(
                 "CREATE TABLE IF NOT EXISTS chats (message_id INTEGER PRIMARY KEY, title TEXT, systemPrompt TEXT, "
                 "sendBehavior TEXT, model TEXT, multiLine TEXT, draftPrompt TEXT, userNote TEXT, version INTEGER "
                 "DEFAULT 0);");
             ok = ok && db.execute(
                            "INSERT OR IGNORE INTO chats (message_id) SELECT DISTINCT target_id FROM settings WHERE "
                            "scope = 'chat';");
             ok = ok && db.execute(
                            "UPDATE chats SET title = (SELECT value FROM settings WHERE scope = 'chat' AND target_id = "
                            "chats.message_id AND key = 'title') WHERE EXISTS (SELECT 1 FROM settings WHERE scope = "
                            "'chat' AND target_id = chats.message_id AND key = 'title');");
             ok =
                 ok && db.execute(
                           "UPDATE chats SET systemPrompt = (SELECT value FROM settings WHERE scope = 'chat' AND "
                           "target_id = chats.message_id AND key = 'systemPrompt') WHERE EXISTS (SELECT 1 FROM "
                           "settings WHERE scope = 'chat' AND target_id = chats.message_id AND key = 'systemPrompt');");
             ok =
                 ok && db.execute(
                           "UPDATE chats SET sendBehavior = (SELECT value FROM settings WHERE scope = 'chat' AND "
                           "target_id = chats.message_id AND key = 'sendBehavior') WHERE EXISTS (SELECT 1 FROM "
                           "settings WHERE scope = 'chat' AND target_id = chats.message_id AND key = 'sendBehavior');");
             ok = ok && db.execute(
                            "UPDATE chats SET model = (SELECT value FROM settings WHERE scope = 'chat' AND target_id = "
                            "chats.message_id AND key = 'model') WHERE EXISTS (SELECT 1 FROM settings WHERE scope = "
                            "'chat' AND target_id = chats.message_id AND key = 'model');");
             ok = ok && db.execute(
                            "UPDATE chats SET multiLine = (SELECT value FROM settings WHERE scope = 'chat' AND "
                            "target_id = chats.message_id AND key = 'multiLine') WHERE EXISTS (SELECT 1 FROM settings "
                            "WHERE scope = 'chat' AND target_id = chats.message_id AND key = 'multiLine');");
             ok = ok && db.execute(
                            "UPDATE chats SET draftPrompt = (SELECT value FROM settings WHERE scope = 'chat' AND "
                            "target_id = chats.message_id AND key = 'draftPrompt') WHERE EXISTS (SELECT 1 FROM "
                            "settings WHERE scope = 'chat' AND target_id = chats.message_id AND key = 'draftPrompt');");
             ok = ok && db.execute(
                            "UPDATE chats SET userNote = (SELECT value FROM settings WHERE scope = 'chat' AND "
                            "target_id = chats.message_id AND key = 'userNote') WHERE EXISTS (SELECT 1 FROM settings "
                            "WHERE scope = 'chat' AND target_id = chats.message_id AND key = 'userNote');");
             ok = ok && db.execute("DELETE FROM settings WHERE scope = 'chat';");
             return ok;
         }});

    runner.addMigration(
        {12, 13, "Document History and Queue Additions", [](Database& db) {
             bool ok = db.execute(
                 "CREATE TABLE IF NOT EXISTS document_history (id INTEGER PRIMARY KEY AUTOINCREMENT, document_id "
                 "INTEGER, action_type TEXT, content TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);");
             ok = ok && db.execute("ALTER TABLE queue ADD COLUMN state TEXT DEFAULT 'pending';");
             ok = ok && db.execute("ALTER TABLE queue ADD COLUMN response TEXT DEFAULT '';");
             ok = ok && db.execute("ALTER TABLE queue ADD COLUMN parent_id INTEGER DEFAULT 0;");
             ok = ok && db.execute("UPDATE queue SET state = 'processing' WHERE processing_id > 0;");
             return ok;
         }});

    runner.addMigration({13, 14, "Queue Target Action", [](Database& db) {
                             return db.execute("ALTER TABLE queue ADD COLUMN target_action TEXT DEFAULT '';");
                         }});

    runner.addMigration({14, 15, "Notifications Target Type", [](Database& db) {
                             bool ok = db.execute("ALTER TABLE notifications RENAME COLUMN message_id TO target_id;");
                             ok = ok && db.execute(
                                            "ALTER TABLE notifications ADD COLUMN target_type TEXT DEFAULT 'message';");
                             return ok;
                         }});

    runner.addMigration({15, 16, "Drafts Target Type and Parent", [](Database& db) {
                             bool ok = db.execute("ALTER TABLE drafts ADD COLUMN parent_id INTEGER DEFAULT 0;");
                             ok =
                                 ok && db.execute("ALTER TABLE drafts ADD COLUMN target_type TEXT DEFAULT 'document';");
                             return ok;
                         }});

    runner.addMigration({16, 17, "Expanded State", [](Database& db) {
                             bool ok = db.execute("ALTER TABLE folders ADD COLUMN is_expanded BOOLEAN DEFAULT 0;");
                             ok = ok && db.execute("ALTER TABLE messages ADD COLUMN is_expanded BOOLEAN DEFAULT 0;");
                             return ok;
                         }});

    runner.addMigration({17, 18, "Documents Metadata", [](Database& db) {
                             return db.execute("ALTER TABLE documents ADD COLUMN metadata TEXT DEFAULT '';");
                         }});

    runner.addMigration({18, 19, "Document Merges", [](Database& db) {
                             return db.execute(
                                 "CREATE TABLE IF NOT EXISTS document_merges ("
                                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                 "document_id INTEGER, "
                                 "source_document_ids TEXT, "
                                 "prompt TEXT, "
                                 "model TEXT, "
                                 "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
                                 "version_history_id INTEGER DEFAULT 0"
                                 ");");
                         }});

    runner.addMigration(
        {19, 20, "Prompt History", [](Database& db) {
             bool ok = db.execute(
                 "CREATE TABLE IF NOT EXISTS prompt_history (id INTEGER PRIMARY KEY AUTOINCREMENT, document_id "
                 "INTEGER, prompt TEXT, model TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, queue_id INTEGER "
                 "DEFAULT 0);");
             ok = ok && db.execute(
                            "INSERT INTO prompt_history (document_id, prompt, model, timestamp) SELECT document_id, "
                            "prompt, model, timestamp FROM document_merges;");
             ok = ok && db.execute(
                            "INSERT INTO prompt_history (document_id, prompt, model, timestamp, queue_id) SELECT "
                            "message_id, prompt, model, created_at, id FROM queue WHERE target_type = 'document';");
             return ok;
         }});

    runner.addMigration({20, 21, "Recreate Document Merges", [](Database& db) {
                             bool ok = db.execute(
                                 "CREATE TABLE IF NOT EXISTS document_merges_new (id INTEGER PRIMARY KEY "
                                 "AUTOINCREMENT, document_id INTEGER, source_document_ids TEXT, timestamp DATETIME "
                                 "DEFAULT CURRENT_TIMESTAMP, version_history_id INTEGER DEFAULT 0);");
                             ok = ok && db.execute(
                                            "INSERT INTO document_merges_new (id, document_id, source_document_ids, "
                                            "timestamp, version_history_id) SELECT id, document_id, "
                                            "source_document_ids, timestamp, version_history_id FROM document_merges;");
                             ok = ok && db.execute("DROP TABLE document_merges;");
                             ok = ok && db.execute("ALTER TABLE document_merges_new RENAME TO document_merges;");
                             return ok;
                         }});

    return runner;
}

}  // namespace db
