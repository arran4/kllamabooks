#include "MigrationFactory.h"

#include <sqlcipher/sqlite3.h>

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
             bool hasCol = false;
             if (!db.hasColumn("documents", "folder_id", hasCol)) return false;
             if (!hasCol) {
                 ok = ok && db.execute("ALTER TABLE documents ADD COLUMN folder_id INTEGER DEFAULT 0;");
             }
             hasCol = false;
             if (!db.hasColumn("notes", "folder_id", hasCol)) return false;
             if (!hasCol) {
                 ok = ok && db.execute("ALTER TABLE notes ADD COLUMN folder_id INTEGER DEFAULT 0;");
             }
             return ok;
         }});

    runner.addMigration({3, 4, "Messages Folder ID", [](Database& db) {
                             bool hasCol = false;
                             if (!db.hasColumn("messages", "folder_id", hasCol)) return false;
                             if (!hasCol) {
                                 return db.execute("ALTER TABLE messages ADD COLUMN folder_id INTEGER DEFAULT 0;");
                             }
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
                             bool hasCol = false;
                             if (!db.hasColumn("documents", "parent_id", hasCol)) return false;
                             if (!hasCol) {
                                 return db.execute("ALTER TABLE documents ADD COLUMN parent_id INTEGER DEFAULT 0;");
                             }
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

    runner.addMigration(
        {21, 22, "Artifact Store Storage vNext", [](Database& db) {
             bool ok = db.execute(
                 "CREATE TABLE IF NOT EXISTS artifacts ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                 "kind TEXT NOT NULL, "
                 "folder_id INTEGER DEFAULT 0, "
                 "current_version_id INTEGER NOT NULL DEFAULT 0, "
                 "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
                 ");");
             ok = ok && db.execute(
                            "CREATE TABLE IF NOT EXISTS artifact_versions ("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                            "artifact_id INTEGER NOT NULL, "
                            "parent_id INTEGER, "
                            "forked_from_version_id INTEGER, "
                            "title TEXT, "
                            "content TEXT, "
                            "metadata TEXT DEFAULT '', "
                            "is_sealed BOOLEAN DEFAULT 0, "
                            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                            "FOREIGN KEY(artifact_id) REFERENCES artifacts(id), "
                            "FOREIGN KEY(parent_id, artifact_id) REFERENCES artifact_versions(id, artifact_id) "
                            "DEFERRABLE INITIALLY DEFERRED, "
                            "FOREIGN KEY(forked_from_version_id) REFERENCES artifact_versions(id) DEFERRABLE INITIALLY "
                            "DEFERRED"
                            ");");
             ok = ok && db.execute(
                            "CREATE UNIQUE INDEX IF NOT EXISTS idx_artifact_versions_unique_id_artifact "
                            "ON artifact_versions(id, artifact_id);");
             ok = ok && db.execute(
                            "CREATE TRIGGER IF NOT EXISTS trg_artifacts_current_version_match_update "
                            "BEFORE UPDATE ON artifacts "
                            "FOR EACH ROW "
                            "WHEN NEW.current_version_id != 0 AND NOT EXISTS (SELECT 1 FROM artifact_versions WHERE id "
                            "= NEW.current_version_id AND artifact_id = NEW.id) "
                            "BEGIN "
                            "  SELECT RAISE(ABORT, 'current_version_id must belong to the same artifact'); "
                            "END;");
             ok = ok && db.execute(
                            "CREATE TRIGGER IF NOT EXISTS trg_artifacts_current_version_match_insert "
                            "BEFORE INSERT ON artifacts "
                            "FOR EACH ROW "
                            "WHEN NEW.current_version_id != 0 "
                            "BEGIN "
                            "  SELECT RAISE(ABORT, 'current_version_id must be 0 on creation'); "
                            "END;");
             ok =
                 ok &&
                 db.execute(
                     "CREATE TRIGGER IF NOT EXISTS trg_artifact_versions_protect_current_update "
                     "BEFORE UPDATE OF artifact_id ON artifact_versions "
                     "FOR EACH ROW "
                     "WHEN EXISTS (SELECT 1 FROM artifacts WHERE id = OLD.artifact_id AND current_version_id = OLD.id) "
                     "BEGIN "
                     "  SELECT RAISE(ABORT, 'Cannot change artifact_id of a version currently referenced as "
                     "current_version_id'); "
                     "END;");
             ok =
                 ok &&
                 db.execute(
                     "CREATE TRIGGER IF NOT EXISTS trg_artifact_versions_protect_current_delete "
                     "BEFORE DELETE ON artifact_versions "
                     "FOR EACH ROW "
                     "WHEN EXISTS (SELECT 1 FROM artifacts WHERE id = OLD.artifact_id AND current_version_id = OLD.id) "
                     "BEGIN "
                     "  SELECT RAISE(ABORT, 'Cannot delete a version currently referenced as current_version_id'); "
                     "END;");
             return ok;
         }});

    runner.addMigration(
        {22, 23, "Migrate Documents to Artifacts", [](Database& db) {
             bool ok = db.execute(
                 "CREATE TABLE IF NOT EXISTS legacy_document_mapping ("
                 "document_id INTEGER PRIMARY KEY, "
                 "artifact_id INTEGER NOT NULL UNIQUE, "
                 "FOREIGN KEY(artifact_id) REFERENCES artifacts(id)"
                 ");");
             if (!ok) return false;

             sqlite3* handle = db.handle();
             const char* selectSql =
                 "SELECT id, folder_id, title, content, timestamp, metadata FROM documents "
                 "WHERE id NOT IN (SELECT document_id FROM legacy_document_mapping);";
             sqlite3_stmt* selectStmt = nullptr;
             if (sqlite3_prepare_v2(handle, selectSql, -1, &selectStmt, nullptr) != SQLITE_OK) {
                 return false;
             }

             const char* insertArtifactSql =
                 "INSERT INTO artifacts (kind, folder_id, current_version_id, created_at) "
                 "VALUES ('document', ?, 0, ?);";
             sqlite3_stmt* insertArtifactStmt = nullptr;
             if (sqlite3_prepare_v2(handle, insertArtifactSql, -1, &insertArtifactStmt, nullptr) != SQLITE_OK) {
                 sqlite3_finalize(selectStmt);
                 return false;
             }

             const char* insertVersionSql =
                 "INSERT INTO artifact_versions (artifact_id, title, content, metadata, created_at) "
                 "VALUES (?, ?, ?, ?, ?);";
             sqlite3_stmt* insertVersionStmt = nullptr;
             if (sqlite3_prepare_v2(handle, insertVersionSql, -1, &insertVersionStmt, nullptr) != SQLITE_OK) {
                 sqlite3_finalize(selectStmt);
                 sqlite3_finalize(insertArtifactStmt);
                 return false;
             }

             const char* updateArtifactSql = "UPDATE artifacts SET current_version_id = ? WHERE id = ?;";
             sqlite3_stmt* updateArtifactStmt = nullptr;
             if (sqlite3_prepare_v2(handle, updateArtifactSql, -1, &updateArtifactStmt, nullptr) != SQLITE_OK) {
                 sqlite3_finalize(selectStmt);
                 sqlite3_finalize(insertArtifactStmt);
                 sqlite3_finalize(insertVersionStmt);
                 return false;
             }

             const char* insertMappingSql =
                 "INSERT INTO legacy_document_mapping (document_id, artifact_id) VALUES (?, ?);";
             sqlite3_stmt* insertMappingStmt = nullptr;
             if (sqlite3_prepare_v2(handle, insertMappingSql, -1, &insertMappingStmt, nullptr) != SQLITE_OK) {
                 sqlite3_finalize(selectStmt);
                 sqlite3_finalize(insertArtifactStmt);
                 sqlite3_finalize(insertVersionStmt);
                 sqlite3_finalize(updateArtifactStmt);
                 return false;
             }

             bool success = true;
             int rc;
             while ((rc = sqlite3_step(selectStmt)) == SQLITE_ROW) {
                 int docId = sqlite3_column_int(selectStmt, 0);
                 int folderId = sqlite3_column_int(selectStmt, 1);
                 const char* title = reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 2));
                 const char* content = reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 3));
                 const char* timestamp = reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 4));
                 const char* metadata = reinterpret_cast<const char*>(sqlite3_column_text(selectStmt, 5));

                 sqlite3_bind_int(insertArtifactStmt, 1, folderId);
                 sqlite3_bind_text(insertArtifactStmt, 2, timestamp, -1, SQLITE_STATIC);
                 if (sqlite3_step(insertArtifactStmt) != SQLITE_DONE) {
                     success = false;
                     break;
                 }
                 sqlite3_int64 artifactId = sqlite3_last_insert_rowid(handle);
                 sqlite3_reset(insertArtifactStmt);

                 sqlite3_bind_int64(insertVersionStmt, 1, artifactId);
                 sqlite3_bind_text(insertVersionStmt, 2, title, -1, SQLITE_STATIC);
                 sqlite3_bind_text(insertVersionStmt, 3, content, -1, SQLITE_STATIC);
                 sqlite3_bind_text(insertVersionStmt, 4, metadata, -1, SQLITE_STATIC);
                 sqlite3_bind_text(insertVersionStmt, 5, timestamp, -1, SQLITE_STATIC);
                 if (sqlite3_step(insertVersionStmt) != SQLITE_DONE) {
                     success = false;
                     break;
                 }
                 sqlite3_int64 versionId = sqlite3_last_insert_rowid(handle);
                 sqlite3_reset(insertVersionStmt);

                 sqlite3_bind_int64(updateArtifactStmt, 1, versionId);
                 sqlite3_bind_int64(updateArtifactStmt, 2, artifactId);
                 if (sqlite3_step(updateArtifactStmt) != SQLITE_DONE) {
                     success = false;
                     break;
                 }
                 sqlite3_reset(updateArtifactStmt);

                 sqlite3_bind_int(insertMappingStmt, 1, docId);
                 sqlite3_bind_int64(insertMappingStmt, 2, artifactId);
                 if (sqlite3_step(insertMappingStmt) != SQLITE_DONE) {
                     success = false;
                     break;
                 }
                 sqlite3_reset(insertMappingStmt);
             }
             if (rc != SQLITE_DONE) {
                 success = false;
             }

             sqlite3_finalize(selectStmt);
             sqlite3_finalize(insertArtifactStmt);
             sqlite3_finalize(insertVersionStmt);
             sqlite3_finalize(updateArtifactStmt);
             sqlite3_finalize(insertMappingStmt);

             return success;
         }});

    return runner;
}

}  // namespace db
