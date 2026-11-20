#include "MigrationFactory.h"

#include <sqlcipher/sqlite3.h>

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

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

    runner.addMigration(
        {23, 24, "Seal Artifact Version Immutability", [](Database& db) {
             bool ok = db.execute(
                 "CREATE TRIGGER IF NOT EXISTS trg_artifact_versions_protect_sealed_update "
                 "BEFORE UPDATE ON artifact_versions "
                 "FOR EACH ROW "
                 "WHEN OLD.is_sealed = 1 AND ("
                 "NEW.id IS NOT OLD.id OR "
                 "NEW.artifact_id IS NOT OLD.artifact_id OR "
                 "NEW.parent_id IS NOT OLD.parent_id OR "
                 "NEW.forked_from_version_id IS NOT OLD.forked_from_version_id OR "
                 "NEW.title IS NOT OLD.title OR "
                 "NEW.content IS NOT OLD.content OR "
                 "NEW.metadata IS NOT OLD.metadata OR "
                 "NEW.is_sealed IS NOT OLD.is_sealed OR "
                 "NEW.created_at IS NOT OLD.created_at"
                 ") "
                 "BEGIN "
                 "  SELECT RAISE(ABORT, 'Cannot mutate a sealed artifact version'); "
                 "END;");
             ok = ok && db.execute(
                            "CREATE TRIGGER IF NOT EXISTS trg_artifact_versions_validate_is_sealed_update "
                            "BEFORE UPDATE OF is_sealed ON artifact_versions "
                            "FOR EACH ROW "
                            "WHEN NEW.is_sealed IS NULL OR (NEW.is_sealed != 0 AND NEW.is_sealed != 1) "
                            "BEGIN "
                            "  SELECT RAISE(ABORT, 'is_sealed must be exactly 0 or 1'); "
                            "END;");
             ok = ok && db.execute(
                            "CREATE TRIGGER IF NOT EXISTS trg_artifact_versions_validate_is_sealed_insert "
                            "BEFORE INSERT ON artifact_versions "
                            "FOR EACH ROW "
                            "WHEN NEW.is_sealed IS NULL OR (NEW.is_sealed != 0 AND NEW.is_sealed != 1) "
                            "BEGIN "
                            "  SELECT RAISE(ABORT, 'is_sealed must be exactly 0 or 1'); "
                            "END;");
             return ok;
         }});

    runner.addMigration(
        {24, 25, "Migrate Unmapped Legacy Artifacts", [](Database& db) {
             sqlite3* handle = db.handle();

             if (!db.execute("CREATE TABLE IF NOT EXISTS legacy_note_mapping ("
                             "note_id INTEGER PRIMARY KEY,"
                             "artifact_id INTEGER NOT NULL UNIQUE REFERENCES artifacts(id) ON DELETE CASCADE"
                             ");"))
                 return false;

             if (!db.execute("CREATE TABLE IF NOT EXISTS legacy_template_mapping ("
                             "template_id INTEGER PRIMARY KEY,"
                             "artifact_id INTEGER NOT NULL UNIQUE REFERENCES artifacts(id) ON DELETE CASCADE"
                             ");"))
                 return false;

             if (!db.execute("CREATE TABLE IF NOT EXISTS legacy_draft_mapping ("
                             "draft_id INTEGER PRIMARY KEY,"
                             "artifact_id INTEGER NOT NULL UNIQUE REFERENCES artifacts(id) ON DELETE CASCADE,"
                             "legacy_parent_id INTEGER,"
                             "legacy_target_type TEXT"
                             ");"))
                 return false;

             sqlite3_stmt* insertArtifactStmt = nullptr;
             sqlite3_stmt* insertVersionStmt = nullptr;
             sqlite3_stmt* updateArtifactStmt = nullptr;

             sqlite3_stmt* insertDocMappingStmt = nullptr;
             sqlite3_stmt* insertNoteMappingStmt = nullptr;
             sqlite3_stmt* insertTemplateMappingStmt = nullptr;
             sqlite3_stmt* insertDraftMappingStmt = nullptr;

             sqlite3_stmt* selectUnmappedDocsStmt = nullptr;
             sqlite3_stmt* selectNotesStmt = nullptr;
             sqlite3_stmt* selectTemplatesStmt = nullptr;
             sqlite3_stmt* selectDraftsStmt = nullptr;

             sqlite3_stmt* selectLegacyDraftMappingsStmt = nullptr;
             sqlite3_stmt* resolveDocLineageStmt = nullptr;
             sqlite3_stmt* resolveNoteLineageStmt = nullptr;
             sqlite3_stmt* resolveTemplateLineageStmt = nullptr;
             sqlite3_stmt* resolveDraftLineageStmt = nullptr;
             sqlite3_stmt* updateForkedVersionStmt = nullptr;

             auto cleanup = [&]() {
                 if (insertArtifactStmt) sqlite3_finalize(insertArtifactStmt);
                 if (insertVersionStmt) sqlite3_finalize(insertVersionStmt);
                 if (updateArtifactStmt) sqlite3_finalize(updateArtifactStmt);
                 if (insertDocMappingStmt) sqlite3_finalize(insertDocMappingStmt);
                 if (insertNoteMappingStmt) sqlite3_finalize(insertNoteMappingStmt);
                 if (insertTemplateMappingStmt) sqlite3_finalize(insertTemplateMappingStmt);
                 if (insertDraftMappingStmt) sqlite3_finalize(insertDraftMappingStmt);
                 if (selectUnmappedDocsStmt) sqlite3_finalize(selectUnmappedDocsStmt);
                 if (selectNotesStmt) sqlite3_finalize(selectNotesStmt);
                 if (selectTemplatesStmt) sqlite3_finalize(selectTemplatesStmt);
                 if (selectDraftsStmt) sqlite3_finalize(selectDraftsStmt);
                 if (selectLegacyDraftMappingsStmt) sqlite3_finalize(selectLegacyDraftMappingsStmt);
                 if (resolveDocLineageStmt) sqlite3_finalize(resolveDocLineageStmt);
                 if (resolveNoteLineageStmt) sqlite3_finalize(resolveNoteLineageStmt);
                 if (resolveTemplateLineageStmt) sqlite3_finalize(resolveTemplateLineageStmt);
                 if (resolveDraftLineageStmt) sqlite3_finalize(resolveDraftLineageStmt);
                 if (updateForkedVersionStmt) sqlite3_finalize(updateForkedVersionStmt);
             };

             auto fail = [&]() {
                 cleanup();
                 return false;
             };

             if (sqlite3_prepare_v2(handle,
                                    "INSERT INTO artifacts (kind, folder_id, created_at) VALUES (?, ?, ?) RETURNING id",
                                    -1, &insertArtifactStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(handle,
                                    "INSERT INTO artifact_versions (artifact_id, title, content, metadata, created_at) "
                                    "VALUES (?, ?, ?, ?, ?) RETURNING id",
                                    -1, &insertVersionStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(handle, "UPDATE artifacts SET current_version_id = ? WHERE id = ?", -1,
                                    &updateArtifactStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(handle,
                                    "INSERT INTO legacy_document_mapping (document_id, artifact_id) VALUES (?, ?)", -1,
                                    &insertDocMappingStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(handle, "INSERT INTO legacy_note_mapping (note_id, artifact_id) VALUES (?, ?)", -1,
                                    &insertNoteMappingStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(handle,
                                    "INSERT INTO legacy_template_mapping (template_id, artifact_id) VALUES (?, ?)", -1,
                                    &insertTemplateMappingStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(handle,
                                    "INSERT INTO legacy_draft_mapping (draft_id, artifact_id, legacy_parent_id, "
                                    "legacy_target_type) VALUES (?, ?, ?, ?)",
                                    -1, &insertDraftMappingStmt, nullptr) != SQLITE_OK)
                 return fail();

             auto insertArtifactAndVersion = [&](const QString& kind, std::optional<sqlite3_int64> folderId,
                                                 const QString& title, const QString& content, const QString& metadata,
                                                 const QString& timestamp, sqlite3_int64& outArtifactId) -> bool {
                 if (sqlite3_reset(insertArtifactStmt) != SQLITE_OK) return false;
                 if (sqlite3_bind_text(insertArtifactStmt, 1, kind.toUtf8().constData(), -1, SQLITE_TRANSIENT) !=
                     SQLITE_OK)
                     return false;
                 if (folderId.has_value()) {
                     if (sqlite3_bind_int64(insertArtifactStmt, 2, folderId.value()) != SQLITE_OK) return false;
                 } else {
                     if (sqlite3_bind_null(insertArtifactStmt, 2) != SQLITE_OK) return false;
                 }
                 if (!timestamp.isNull()) {
                     if (sqlite3_bind_text(insertArtifactStmt, 3, timestamp.toUtf8().constData(), -1,
                                           SQLITE_TRANSIENT) != SQLITE_OK)
                         return false;
                 } else {
                     if (sqlite3_bind_null(insertArtifactStmt, 3) != SQLITE_OK) return false;
                 }

                 if (sqlite3_step(insertArtifactStmt) != SQLITE_ROW) return false;
                 outArtifactId = sqlite3_column_int64(insertArtifactStmt, 0);

                 if (sqlite3_reset(insertVersionStmt) != SQLITE_OK) return false;
                 if (sqlite3_bind_int64(insertVersionStmt, 1, outArtifactId) != SQLITE_OK) return false;

                 if (!title.isNull()) {
                     if (sqlite3_bind_text(insertVersionStmt, 2, title.toUtf8().constData(), -1, SQLITE_TRANSIENT) !=
                         SQLITE_OK)
                         return false;
                 } else {
                     if (sqlite3_bind_null(insertVersionStmt, 2) != SQLITE_OK) return false;
                 }

                 if (!content.isNull()) {
                     if (sqlite3_bind_text(insertVersionStmt, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT) !=
                         SQLITE_OK)
                         return false;
                 } else {
                     if (sqlite3_bind_null(insertVersionStmt, 3) != SQLITE_OK) return false;
                 }

                 if (!metadata.isNull()) {
                     if (sqlite3_bind_text(insertVersionStmt, 4, metadata.toUtf8().constData(), -1, SQLITE_TRANSIENT) !=
                         SQLITE_OK)
                         return false;
                 } else {
                     if (sqlite3_bind_null(insertVersionStmt, 4) != SQLITE_OK) return false;
                 }

                 if (!timestamp.isNull()) {
                     if (sqlite3_bind_text(insertVersionStmt, 5, timestamp.toUtf8().constData(), -1,
                                           SQLITE_TRANSIENT) != SQLITE_OK)
                         return false;
                 } else {
                     if (sqlite3_bind_null(insertVersionStmt, 5) != SQLITE_OK) return false;
                 }

                 if (sqlite3_step(insertVersionStmt) != SQLITE_ROW) return false;
                 sqlite3_int64 versionId = sqlite3_column_int64(insertVersionStmt, 0);

                 if (sqlite3_reset(updateArtifactStmt) != SQLITE_OK) return false;
                 if (sqlite3_bind_int64(updateArtifactStmt, 1, versionId) != SQLITE_OK) return false;
                 if (sqlite3_bind_int64(updateArtifactStmt, 2, outArtifactId) != SQLITE_OK) return false;
                 if (sqlite3_step(updateArtifactStmt) != SQLITE_DONE) return false;

                 return true;
             };

             // 1. Documents
             if (sqlite3_prepare_v2(
                     handle,
                     "SELECT id, folder_id, title, content, metadata, timestamp FROM documents WHERE id NOT "
                     "IN (SELECT document_id FROM legacy_document_mapping)",
                     -1, &selectUnmappedDocsStmt, nullptr) != SQLITE_OK)
                 return fail();

             int rcDocs;
             while ((rcDocs = sqlite3_step(selectUnmappedDocsStmt)) == SQLITE_ROW) {
                 sqlite3_int64 docId = sqlite3_column_int64(selectUnmappedDocsStmt, 0);
                 std::optional<sqlite3_int64> folderId;
                 if (sqlite3_column_type(selectUnmappedDocsStmt, 1) != SQLITE_NULL) {
                     folderId = sqlite3_column_int64(selectUnmappedDocsStmt, 1);
                 }
                 QString title;
                 if (sqlite3_column_type(selectUnmappedDocsStmt, 2) != SQLITE_NULL)
                     title = QString::fromUtf8(
                         reinterpret_cast<const char*>(sqlite3_column_text(selectUnmappedDocsStmt, 2)));
                 QString content;
                 if (sqlite3_column_type(selectUnmappedDocsStmt, 3) != SQLITE_NULL)
                     content = QString::fromUtf8(
                         reinterpret_cast<const char*>(sqlite3_column_text(selectUnmappedDocsStmt, 3)));
                 QString metadata;
                 if (sqlite3_column_type(selectUnmappedDocsStmt, 4) != SQLITE_NULL)
                     metadata = QString::fromUtf8(
                         reinterpret_cast<const char*>(sqlite3_column_text(selectUnmappedDocsStmt, 4)));
                 QString timestamp;
                 if (sqlite3_column_type(selectUnmappedDocsStmt, 5) != SQLITE_NULL)
                     timestamp = QString::fromUtf8(
                         reinterpret_cast<const char*>(sqlite3_column_text(selectUnmappedDocsStmt, 5)));

                 sqlite3_int64 artifactId;
                 if (!insertArtifactAndVersion("document", folderId, title, content, metadata, timestamp, artifactId))
                     return fail();

                 if (sqlite3_reset(insertDocMappingStmt) != SQLITE_OK) return fail();
                 if (sqlite3_bind_int64(insertDocMappingStmt, 1, docId) != SQLITE_OK) return fail();
                 if (sqlite3_bind_int64(insertDocMappingStmt, 2, artifactId) != SQLITE_OK) return fail();
                 if (sqlite3_step(insertDocMappingStmt) != SQLITE_DONE) return fail();
             }
             if (rcDocs != SQLITE_DONE) return fail();

             // 2. Notes
             if (sqlite3_prepare_v2(handle,
                                    "SELECT id, folder_id, title, content, timestamp FROM notes WHERE id NOT "
                                    "IN (SELECT note_id FROM legacy_note_mapping)",
                                    -1, &selectNotesStmt, nullptr) != SQLITE_OK)
                 return fail();

             int rc;
             while ((rc = sqlite3_step(selectNotesStmt)) == SQLITE_ROW) {
                 sqlite3_int64 noteId = sqlite3_column_int64(selectNotesStmt, 0);
                 std::optional<sqlite3_int64> folderId;
                 if (sqlite3_column_type(selectNotesStmt, 1) != SQLITE_NULL) {
                     folderId = sqlite3_column_int64(selectNotesStmt, 1);
                 }
                 QString title;
                 if (sqlite3_column_type(selectNotesStmt, 2) != SQLITE_NULL)
                     title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(selectNotesStmt, 2)));
                 QString content;
                 if (sqlite3_column_type(selectNotesStmt, 3) != SQLITE_NULL)
                     content =
                         QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(selectNotesStmt, 3)));
                 QString timestamp;
                 if (sqlite3_column_type(selectNotesStmt, 4) != SQLITE_NULL)
                     timestamp =
                         QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(selectNotesStmt, 4)));

                 sqlite3_int64 artifactId;
                 if (!insertArtifactAndVersion("note", folderId, title, content, QString(), timestamp, artifactId))
                     return fail();

                 if (sqlite3_reset(insertNoteMappingStmt) != SQLITE_OK) return fail();
                 if (sqlite3_bind_int64(insertNoteMappingStmt, 1, noteId) != SQLITE_OK) return fail();
                 if (sqlite3_bind_int64(insertNoteMappingStmt, 2, artifactId) != SQLITE_OK) return fail();
                 if (sqlite3_step(insertNoteMappingStmt) != SQLITE_DONE) return fail();
             }
             if (rc != SQLITE_DONE) return fail();

             // 3. Templates
             if (sqlite3_prepare_v2(handle,
                                    "SELECT id, folder_id, title, content, timestamp FROM templates WHERE id NOT "
                                    "IN (SELECT template_id FROM legacy_template_mapping)",
                                    -1, &selectTemplatesStmt, nullptr) != SQLITE_OK)
                 return fail();

             while ((rc = sqlite3_step(selectTemplatesStmt)) == SQLITE_ROW) {
                 sqlite3_int64 templateId = sqlite3_column_int64(selectTemplatesStmt, 0);
                 std::optional<sqlite3_int64> folderId;
                 if (sqlite3_column_type(selectTemplatesStmt, 1) != SQLITE_NULL) {
                     folderId = sqlite3_column_int64(selectTemplatesStmt, 1);
                 }
                 QString title;
                 if (sqlite3_column_type(selectTemplatesStmt, 2) != SQLITE_NULL)
                     title =
                         QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(selectTemplatesStmt, 2)));
                 QString content;
                 if (sqlite3_column_type(selectTemplatesStmt, 3) != SQLITE_NULL)
                     content =
                         QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(selectTemplatesStmt, 3)));
                 QString timestamp;
                 if (sqlite3_column_type(selectTemplatesStmt, 4) != SQLITE_NULL)
                     timestamp =
                         QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(selectTemplatesStmt, 4)));

                 sqlite3_int64 artifactId;
                 if (!insertArtifactAndVersion("template", folderId, title, content, QString(), timestamp, artifactId))
                     return fail();

                 if (sqlite3_reset(insertTemplateMappingStmt) != SQLITE_OK) return fail();
                 if (sqlite3_bind_int64(insertTemplateMappingStmt, 1, templateId) != SQLITE_OK) return fail();
                 if (sqlite3_bind_int64(insertTemplateMappingStmt, 2, artifactId) != SQLITE_OK) return fail();
                 if (sqlite3_step(insertTemplateMappingStmt) != SQLITE_DONE) return fail();
             }
             if (rc != SQLITE_DONE) return fail();

             // 4. Drafts (Phase A - Insert)
             if (sqlite3_prepare_v2(
                     handle,
                     "SELECT id, folder_id, title, content, parent_id, target_type, timestamp FROM drafts WHERE id NOT "
                     "IN (SELECT draft_id FROM legacy_draft_mapping)",
                     -1, &selectDraftsStmt, nullptr) != SQLITE_OK)
                 return fail();

             int rcDrafts;
             while ((rcDrafts = sqlite3_step(selectDraftsStmt)) == SQLITE_ROW) {
                 sqlite3_int64 draftId = sqlite3_column_int64(selectDraftsStmt, 0);
                 std::optional<sqlite3_int64> folderId;
                 if (sqlite3_column_type(selectDraftsStmt, 1) != SQLITE_NULL) {
                     folderId = sqlite3_column_int64(selectDraftsStmt, 1);
                 }
                 QString title;
                 if (sqlite3_column_type(selectDraftsStmt, 2) != SQLITE_NULL)
                     title = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(selectDraftsStmt, 2)));
                 QString content;
                 if (sqlite3_column_type(selectDraftsStmt, 3) != SQLITE_NULL)
                     content =
                         QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(selectDraftsStmt, 3)));

                 std::optional<sqlite3_int64> legacyParentId;
                 if (sqlite3_column_type(selectDraftsStmt, 4) != SQLITE_NULL) {
                     legacyParentId = sqlite3_column_int64(selectDraftsStmt, 4);
                 }

                 std::optional<QString> legacyTargetType;
                 if (sqlite3_column_type(selectDraftsStmt, 5) != SQLITE_NULL) {
                     legacyTargetType =
                         QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(selectDraftsStmt, 5)));
                 }

                 QString timestamp;
                 if (sqlite3_column_type(selectDraftsStmt, 6) != SQLITE_NULL)
                     timestamp =
                         QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(selectDraftsStmt, 6)));

                 sqlite3_int64 artifactId;
                 if (!insertArtifactAndVersion("draft", folderId, title, content, QString(), timestamp, artifactId))
                     return fail();

                 if (sqlite3_reset(insertDraftMappingStmt) != SQLITE_OK) return fail();
                 if (sqlite3_bind_int64(insertDraftMappingStmt, 1, draftId) != SQLITE_OK) return fail();
                 if (sqlite3_bind_int64(insertDraftMappingStmt, 2, artifactId) != SQLITE_OK) return fail();
                 if (legacyParentId.has_value()) {
                     if (sqlite3_bind_int64(insertDraftMappingStmt, 3, legacyParentId.value()) != SQLITE_OK)
                         return fail();
                 } else {
                     if (sqlite3_bind_null(insertDraftMappingStmt, 3) != SQLITE_OK) return fail();
                 }
                 if (legacyTargetType.has_value()) {
                     if (sqlite3_bind_text(insertDraftMappingStmt, 4, legacyTargetType.value().toUtf8().constData(), -1,
                                           SQLITE_TRANSIENT) != SQLITE_OK)
                         return fail();
                 } else {
                     if (sqlite3_bind_null(insertDraftMappingStmt, 4) != SQLITE_OK) return fail();
                 }

                 if (sqlite3_step(insertDraftMappingStmt) != SQLITE_DONE) return fail();
             }
             if (rcDrafts != SQLITE_DONE) return fail();

             // Drafts Phase B: Lineage resolution
             if (sqlite3_prepare_v2(
                     handle,
                     "SELECT artifact_id, legacy_parent_id, legacy_target_type FROM legacy_draft_mapping "
                     "WHERE legacy_parent_id IS NOT NULL AND legacy_target_type IS NOT NULL",
                     -1, &selectLegacyDraftMappingsStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(handle,
                                    "SELECT a.current_version_id FROM legacy_document_mapping m JOIN artifacts a ON "
                                    "a.id = m.artifact_id "
                                    "WHERE m.document_id = ?",
                                    -1, &resolveDocLineageStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(
                     handle,
                     "SELECT a.current_version_id FROM legacy_note_mapping m JOIN artifacts a ON a.id = m.artifact_id "
                     "WHERE m.note_id = ?",
                     -1, &resolveNoteLineageStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(handle,
                                    "SELECT a.current_version_id FROM legacy_template_mapping m JOIN artifacts a ON "
                                    "a.id = m.artifact_id "
                                    "WHERE m.template_id = ?",
                                    -1, &resolveTemplateLineageStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(
                     handle,
                     "SELECT a.current_version_id FROM legacy_draft_mapping m JOIN artifacts a ON a.id = m.artifact_id "
                     "WHERE m.draft_id = ?",
                     -1, &resolveDraftLineageStmt, nullptr) != SQLITE_OK)
                 return fail();

             if (sqlite3_prepare_v2(handle,
                                    "UPDATE artifact_versions SET forked_from_version_id = ? WHERE id = (SELECT "
                                    "current_version_id FROM artifacts WHERE id = ?)",
                                    -1, &updateForkedVersionStmt, nullptr) != SQLITE_OK)
                 return fail();

             while ((rc = sqlite3_step(selectLegacyDraftMappingsStmt)) == SQLITE_ROW) {
                 sqlite3_int64 artifactId = sqlite3_column_int64(selectLegacyDraftMappingsStmt, 0);
                 sqlite3_int64 parentId = sqlite3_column_int64(selectLegacyDraftMappingsStmt, 1);
                 QString targetType = QString::fromUtf8(
                     reinterpret_cast<const char*>(sqlite3_column_text(selectLegacyDraftMappingsStmt, 2)));

                 sqlite3_stmt* resolveStmt = nullptr;
                 if (targetType == "document")
                     resolveStmt = resolveDocLineageStmt;
                 else if (targetType == "note")
                     resolveStmt = resolveNoteLineageStmt;
                 else if (targetType == "template")
                     resolveStmt = resolveTemplateLineageStmt;
                 else if (targetType == "draft")
                     resolveStmt = resolveDraftLineageStmt;

                 if (resolveStmt) {
                     if (sqlite3_reset(resolveStmt) != SQLITE_OK) return fail();
                     if (sqlite3_bind_int64(resolveStmt, 1, parentId) != SQLITE_OK) return fail();

                     int resRc = sqlite3_step(resolveStmt);
                     if (resRc == SQLITE_ROW) {
                         sqlite3_int64 parentVersionId = sqlite3_column_int64(resolveStmt, 0);

                         if (sqlite3_reset(updateForkedVersionStmt) != SQLITE_OK) return fail();
                         if (sqlite3_bind_int64(updateForkedVersionStmt, 1, parentVersionId) != SQLITE_OK)
                             return fail();
                         if (sqlite3_bind_int64(updateForkedVersionStmt, 2, artifactId) != SQLITE_OK) return fail();
                         if (sqlite3_step(updateForkedVersionStmt) != SQLITE_DONE) return fail();
                     } else if (resRc != SQLITE_DONE) {
                         return fail();  // Database error, not just 'no resolution'
                     }
                 }
             }
             if (rc != SQLITE_DONE) return fail();

             sqlite3_stmt* sealStmt = nullptr;
             if (sqlite3_prepare_v2(
                     handle,
                     "UPDATE artifact_versions SET is_sealed = 1 WHERE id IN (SELECT "
                     "forked_from_version_id FROM artifact_versions WHERE forked_from_version_id IS NOT NULL)",
                     -1, &sealStmt, nullptr) != SQLITE_OK) {
                 cleanup();
                 return false;
             }
             if (sqlite3_step(sealStmt) != SQLITE_DONE) {
                 sqlite3_finalize(sealStmt);
                 cleanup();
                 return false;
             }
             sqlite3_finalize(sealStmt);

             cleanup();
             return true;
         }});

    runner.addMigration(
        {25, 26, "Create quarantined_records table for orphaned and recovered data", [](Database& db) {
             if (!db.execute("CREATE TABLE quarantined_records ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "source_table TEXT NOT NULL, "
                             "legacy_id INTEGER, "
                             "target_type TEXT NOT NULL DEFAULT '', "
                             "parent_reference INTEGER, "
                             "title TEXT, "
                             "content TEXT, "
                             "raw_payload_json TEXT NOT NULL, "
                             "reason TEXT NOT NULL, "
                             "status TEXT NOT NULL DEFAULT 'quarantined' "
                             "CHECK(status IN ('quarantined', 'reimported', 'reassigned', 'deleted')), "
                             "resolved_target_kind TEXT, "
                             "resolved_target_id INTEGER, "
                             "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                             "resolved_at DATETIME"
                             ");")) {
                 return false;
             }
             if (!db.execute("CREATE INDEX idx_quarantined_records_status ON quarantined_records(status);")) {
                 return false;
             }
             if (!db.execute(
                     "CREATE INDEX idx_quarantined_records_source ON quarantined_records(source_table, legacy_id);")) {
                 return false;
             }
             return true;
         }});

    runner.addMigration(
        {26, 27, "Catch-Up Legacy Artifacts, Migrate Document History, and Quarantine Orphans", [](Database& db) {
             sqlite3* handle = db.handle();

             if (!db.execute("CREATE TABLE IF NOT EXISTS legacy_document_history_mapping ("
                             "history_id INTEGER PRIMARY KEY, "
                             "artifact_version_id INTEGER NOT NULL UNIQUE REFERENCES artifact_versions(id) ON DELETE "
                             "CASCADE"
                             ");")) {
                 return false;
             }

             auto colText = [](sqlite3_stmt* s, int idx) -> QString {
                 if (sqlite3_column_type(s, idx) == SQLITE_NULL) return QString();
                 const unsigned char* t = sqlite3_column_text(s, idx);
                 return t ? QString::fromUtf8(reinterpret_cast<const char*>(t)) : QString();
             };

             auto insertArtifactAndVersion = [&](const QString& kind, std::optional<sqlite3_int64> folderId,
                                                 const QString& title, const QString& content, const QString& metadata,
                                                 const QString& timestamp, sqlite3_int64& outArtifactId) -> bool {
                 sqlite3_stmt* artStmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "INSERT INTO artifacts (kind, folder_id, created_at) VALUES (?, ?, ?) "
                                        "RETURNING id;",
                                        -1, &artStmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 const QByteArray kindUtf8 = kind.toUtf8();
                 sqlite3_bind_text(artStmt, 1, kindUtf8.constData(), -1, SQLITE_TRANSIENT);
                 if (folderId.has_value()) {
                     sqlite3_bind_int64(artStmt, 2, *folderId);
                 } else {
                     sqlite3_bind_null(artStmt, 2);
                 }
                 if (!timestamp.isNull()) {
                     const QByteArray tsUtf8 = timestamp.toUtf8();
                     sqlite3_bind_text(artStmt, 3, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
                 } else {
                     sqlite3_bind_null(artStmt, 3);
                 }
                 if (sqlite3_step(artStmt) != SQLITE_ROW) {
                     sqlite3_finalize(artStmt);
                     return false;
                 }
                 outArtifactId = sqlite3_column_int64(artStmt, 0);
                 sqlite3_finalize(artStmt);

                 sqlite3_stmt* verStmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "INSERT INTO artifact_versions (artifact_id, title, content, metadata, "
                                        "created_at) VALUES (?, ?, ?, ?, ?) RETURNING id;",
                                        -1, &verStmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 sqlite3_bind_int64(verStmt, 1, outArtifactId);
                 if (!title.isNull()) {
                     const QByteArray titleUtf8 = title.toUtf8();
                     sqlite3_bind_text(verStmt, 2, titleUtf8.constData(), -1, SQLITE_TRANSIENT);
                 } else {
                     sqlite3_bind_null(verStmt, 2);
                 }
                 if (!content.isNull()) {
                     const QByteArray contentUtf8 = content.toUtf8();
                     sqlite3_bind_text(verStmt, 3, contentUtf8.constData(), -1, SQLITE_TRANSIENT);
                 } else {
                     sqlite3_bind_null(verStmt, 3);
                 }
                 if (!metadata.isNull()) {
                     const QByteArray metaUtf8 = metadata.toUtf8();
                     sqlite3_bind_text(verStmt, 4, metaUtf8.constData(), -1, SQLITE_TRANSIENT);
                 } else {
                     sqlite3_bind_null(verStmt, 4);
                 }
                 if (!timestamp.isNull()) {
                     const QByteArray tsUtf8 = timestamp.toUtf8();
                     sqlite3_bind_text(verStmt, 5, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
                 } else {
                     sqlite3_bind_null(verStmt, 5);
                 }
                 if (sqlite3_step(verStmt) != SQLITE_ROW) {
                     sqlite3_finalize(verStmt);
                     return false;
                 }
                 sqlite3_int64 verId = sqlite3_column_int64(verStmt, 0);
                 sqlite3_finalize(verStmt);

                 sqlite3_stmt* updStmt = nullptr;
                 if (sqlite3_prepare_v2(handle, "UPDATE artifacts SET current_version_id = ? WHERE id = ?;", -1,
                                        &updStmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 sqlite3_bind_int64(updStmt, 1, verId);
                 sqlite3_bind_int64(updStmt, 2, outArtifactId);
                 bool ok = sqlite3_step(updStmt) == SQLITE_DONE;
                 sqlite3_finalize(updStmt);
                 return ok;
             };

             auto syncMappedArtifact = [&](sqlite3_int64 artifactId, std::optional<sqlite3_int64> folderId,
                                           const QString& title, const QString& content, const QString& metadata,
                                           const QString& timestamp) -> bool {
                 sqlite3_stmt* curStmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT a.folder_id, v.id, v.title, v.content, v.metadata, v.is_sealed "
                                        "FROM artifacts a JOIN artifact_versions v ON v.id = a.current_version_id "
                                        "WHERE a.id = ?;",
                                        -1, &curStmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 sqlite3_bind_int64(curStmt, 1, artifactId);
                 if (sqlite3_step(curStmt) != SQLITE_ROW) {
                     sqlite3_finalize(curStmt);
                     return false;
                 }
                 std::optional<sqlite3_int64> curFolderId;
                 if (sqlite3_column_type(curStmt, 0) != SQLITE_NULL) {
                     curFolderId = sqlite3_column_int64(curStmt, 0);
                 }
                 sqlite3_int64 curVerId = sqlite3_column_int64(curStmt, 1);
                 QString curTitle = colText(curStmt, 2);
                 QString curContent = colText(curStmt, 3);
                 QString curMetadata = colText(curStmt, 4);
                 bool isSealed = sqlite3_column_int(curStmt, 5) != 0;
                 sqlite3_finalize(curStmt);

                 if (curFolderId != folderId) {
                     sqlite3_stmt* fStmt = nullptr;
                     if (sqlite3_prepare_v2(handle, "UPDATE artifacts SET folder_id = ? WHERE id = ?;", -1, &fStmt,
                                            nullptr) != SQLITE_OK) {
                         return false;
                     }
                     if (folderId.has_value()) {
                         sqlite3_bind_int64(fStmt, 1, *folderId);
                     } else {
                         sqlite3_bind_null(fStmt, 1);
                     }
                     sqlite3_bind_int64(fStmt, 2, artifactId);
                     bool ok = sqlite3_step(fStmt) == SQLITE_DONE;
                     sqlite3_finalize(fStmt);
                     if (!ok) return false;
                 }

                 const QString normMeta = metadata.isNull() ? QString("") : metadata;
                 const QString normCurMeta = curMetadata.isNull() ? QString("") : curMetadata;
                 const bool titleChanged =
                     (title.isNull() != curTitle.isNull()) || (!title.isNull() && title != curTitle);
                 const bool contentChanged =
                     (content.isNull() != curContent.isNull()) || (!content.isNull() && content != curContent);
                 const bool metaChanged = normMeta != normCurMeta;

                 if (!titleChanged && !contentChanged && !metaChanged) {
                     return true;
                 }

                 if (!isSealed) {
                     sqlite3_stmt* uStmt = nullptr;
                     if (sqlite3_prepare_v2(
                             handle, "UPDATE artifact_versions SET title = ?, content = ?, metadata = ? WHERE id = ?;",
                             -1, &uStmt, nullptr) != SQLITE_OK) {
                         return false;
                     }
                     if (!title.isNull()) {
                         const QByteArray tUtf8 = title.toUtf8();
                         sqlite3_bind_text(uStmt, 1, tUtf8.constData(), -1, SQLITE_TRANSIENT);
                     } else {
                         sqlite3_bind_null(uStmt, 1);
                     }
                     if (!content.isNull()) {
                         const QByteArray cUtf8 = content.toUtf8();
                         sqlite3_bind_text(uStmt, 2, cUtf8.constData(), -1, SQLITE_TRANSIENT);
                     } else {
                         sqlite3_bind_null(uStmt, 2);
                     }
                     const QByteArray mUtf8 = normMeta.toUtf8();
                     sqlite3_bind_text(uStmt, 3, mUtf8.constData(), -1, SQLITE_TRANSIENT);
                     sqlite3_bind_int64(uStmt, 4, curVerId);
                     bool ok = sqlite3_step(uStmt) == SQLITE_DONE;
                     sqlite3_finalize(uStmt);
                     return ok;
                 }

                 sqlite3_stmt* nStmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "INSERT INTO artifact_versions (artifact_id, parent_id, title, content, "
                                        "metadata, is_sealed, created_at) VALUES (?, ?, ?, ?, ?, 0, ?) RETURNING id;",
                                        -1, &nStmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 sqlite3_bind_int64(nStmt, 1, artifactId);
                 sqlite3_bind_int64(nStmt, 2, curVerId);
                 if (!title.isNull()) {
                     const QByteArray tUtf8 = title.toUtf8();
                     sqlite3_bind_text(nStmt, 3, tUtf8.constData(), -1, SQLITE_TRANSIENT);
                 } else {
                     sqlite3_bind_null(nStmt, 3);
                 }
                 if (!content.isNull()) {
                     const QByteArray cUtf8 = content.toUtf8();
                     sqlite3_bind_text(nStmt, 4, cUtf8.constData(), -1, SQLITE_TRANSIENT);
                 } else {
                     sqlite3_bind_null(nStmt, 4);
                 }
                 const QByteArray mUtf8 = normMeta.toUtf8();
                 sqlite3_bind_text(nStmt, 5, mUtf8.constData(), -1, SQLITE_TRANSIENT);
                 if (!timestamp.isNull()) {
                     const QByteArray tsUtf8 = timestamp.toUtf8();
                     sqlite3_bind_text(nStmt, 6, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
                 } else {
                     sqlite3_bind_null(nStmt, 6);
                 }
                 if (sqlite3_step(nStmt) != SQLITE_ROW) {
                     sqlite3_finalize(nStmt);
                     return false;
                 }
                 sqlite3_int64 newVerId = sqlite3_column_int64(nStmt, 0);
                 sqlite3_finalize(nStmt);

                 sqlite3_stmt* aStmt = nullptr;
                 if (sqlite3_prepare_v2(handle, "UPDATE artifacts SET current_version_id = ? WHERE id = ?;", -1, &aStmt,
                                        nullptr) != SQLITE_OK) {
                     return false;
                 }
                 sqlite3_bind_int64(aStmt, 1, newVerId);
                 sqlite3_bind_int64(aStmt, 2, artifactId);
                 bool ok = sqlite3_step(aStmt) == SQLITE_DONE;
                 sqlite3_finalize(aStmt);
                 return ok;
             };

             auto insertQuarantineIfMissing = [&](const QString& sourceTable, std::optional<sqlite3_int64> legacyId,
                                                  const QString& targetType, std::optional<sqlite3_int64> parentRef,
                                                  const QString& title, const QString& content,
                                                  const QString& rawPayloadJson, const QString& reason) -> bool {
                 if (legacyId.has_value()) {
                     sqlite3_stmt* chkStmt = nullptr;
                     if (sqlite3_prepare_v2(
                             handle, "SELECT 1 FROM quarantined_records WHERE source_table = ? AND legacy_id = ?;", -1,
                             &chkStmt, nullptr) != SQLITE_OK) {
                         return false;
                     }
                     const QByteArray sUtf8 = sourceTable.toUtf8();
                     sqlite3_bind_text(chkStmt, 1, sUtf8.constData(), -1, SQLITE_TRANSIENT);
                     sqlite3_bind_int64(chkStmt, 2, *legacyId);
                     int chkRc = sqlite3_step(chkStmt);
                     sqlite3_finalize(chkStmt);
                     if (chkRc == SQLITE_ROW) return true;
                     if (chkRc != SQLITE_DONE) return false;
                 }

                 sqlite3_stmt* qStmt = nullptr;
                 if (sqlite3_prepare_v2(
                         handle,
                         "INSERT INTO quarantined_records (source_table, legacy_id, target_type, parent_reference, "
                         "title, content, raw_payload_json, reason, status) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'quarantined');",
                         -1, &qStmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 const QByteArray sUtf8 = sourceTable.toUtf8();
                 const QByteArray tUtf8 = targetType.toUtf8();
                 const QByteArray pUtf8 = rawPayloadJson.toUtf8();
                 const QByteArray rUtf8 = reason.toUtf8();
                 sqlite3_bind_text(qStmt, 1, sUtf8.constData(), -1, SQLITE_TRANSIENT);
                 if (legacyId.has_value()) {
                     sqlite3_bind_int64(qStmt, 2, *legacyId);
                 } else {
                     sqlite3_bind_null(qStmt, 2);
                 }
                 sqlite3_bind_text(qStmt, 3, tUtf8.constData(), -1, SQLITE_TRANSIENT);
                 if (parentRef.has_value()) {
                     sqlite3_bind_int64(qStmt, 4, *parentRef);
                 } else {
                     sqlite3_bind_null(qStmt, 4);
                 }
                 if (!title.isNull()) {
                     const QByteArray titleUtf8 = title.toUtf8();
                     sqlite3_bind_text(qStmt, 5, titleUtf8.constData(), -1, SQLITE_TRANSIENT);
                 } else {
                     sqlite3_bind_null(qStmt, 5);
                 }
                 if (!content.isNull()) {
                     const QByteArray contentUtf8 = content.toUtf8();
                     sqlite3_bind_text(qStmt, 6, contentUtf8.constData(), -1, SQLITE_TRANSIENT);
                 } else {
                     sqlite3_bind_null(qStmt, 6);
                 }
                 sqlite3_bind_text(qStmt, 7, pUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(qStmt, 8, rUtf8.constData(), -1, SQLITE_TRANSIENT);

                 bool ok = sqlite3_step(qStmt) == SQLITE_DONE;
                 sqlite3_finalize(qStmt);
                 return ok;
             };

             // 1. Catch-up documents (unmapped + modified)
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT d.id, d.folder_id, d.title, d.content, d.metadata, d.timestamp, "
                                        "m.artifact_id FROM documents d "
                                        "LEFT JOIN legacy_document_mapping m ON m.document_id = d.id ORDER BY d.id "
                                        "ASC;",
                                        -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     sqlite3_int64 docId = sqlite3_column_int64(stmt, 0);
                     std::optional<sqlite3_int64> folderId;
                     if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) folderId = sqlite3_column_int64(stmt, 1);
                     QString title = colText(stmt, 2);
                     QString content = colText(stmt, 3);
                     QString metadata = colText(stmt, 4);
                     QString timestamp = colText(stmt, 5);

                     if (sqlite3_column_type(stmt, 6) == SQLITE_NULL) {
                         sqlite3_int64 artId = 0;
                         if (!insertArtifactAndVersion("document", folderId, title, content, metadata, timestamp,
                                                       artId)) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         if (!db.execute(QString("INSERT INTO legacy_document_mapping (document_id, artifact_id) "
                                                 "VALUES (%1, %2);")
                                             .arg(docId)
                                             .arg(artId))) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     } else {
                         sqlite3_int64 artId = sqlite3_column_int64(stmt, 6);
                         if (!syncMappedArtifact(artId, folderId, title, content, metadata, timestamp)) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             // 2. Catch-up notes (unmapped + modified)
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT n.id, n.folder_id, n.title, n.content, n.timestamp, m.artifact_id "
                                        "FROM notes n LEFT JOIN legacy_note_mapping m ON m.note_id = n.id ORDER BY "
                                        "n.id ASC;",
                                        -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     sqlite3_int64 noteId = sqlite3_column_int64(stmt, 0);
                     std::optional<sqlite3_int64> folderId;
                     if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) folderId = sqlite3_column_int64(stmt, 1);
                     QString title = colText(stmt, 2);
                     QString content = colText(stmt, 3);
                     QString timestamp = colText(stmt, 4);

                     if (sqlite3_column_type(stmt, 5) == SQLITE_NULL) {
                         sqlite3_int64 artId = 0;
                         if (!insertArtifactAndVersion("note", folderId, title, content, QString(), timestamp, artId)) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         if (!db.execute(
                                 QString("INSERT INTO legacy_note_mapping (note_id, artifact_id) VALUES (%1, %2);")
                                     .arg(noteId)
                                     .arg(artId))) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     } else {
                         sqlite3_int64 artId = sqlite3_column_int64(stmt, 5);
                         if (!syncMappedArtifact(artId, folderId, title, content, QString(""), timestamp)) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             // 3. Catch-up templates (unmapped + modified)
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT t.id, t.folder_id, t.title, t.content, t.timestamp, m.artifact_id "
                                        "FROM templates t LEFT JOIN legacy_template_mapping m ON m.template_id = t.id "
                                        "ORDER BY t.id ASC;",
                                        -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     sqlite3_int64 tmplId = sqlite3_column_int64(stmt, 0);
                     std::optional<sqlite3_int64> folderId;
                     if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) folderId = sqlite3_column_int64(stmt, 1);
                     QString title = colText(stmt, 2);
                     QString content = colText(stmt, 3);
                     QString timestamp = colText(stmt, 4);

                     if (sqlite3_column_type(stmt, 5) == SQLITE_NULL) {
                         sqlite3_int64 artId = 0;
                         if (!insertArtifactAndVersion("template", folderId, title, content, QString(), timestamp,
                                                       artId)) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         if (!db.execute(QString("INSERT INTO legacy_template_mapping (template_id, artifact_id) "
                                                 "VALUES (%1, %2);")
                                             .arg(tmplId)
                                             .arg(artId))) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     } else {
                         sqlite3_int64 artId = sqlite3_column_int64(stmt, 5);
                         if (!syncMappedArtifact(artId, folderId, title, content, QString(""), timestamp)) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             // 4. Catch-up drafts (unmapped + modified) and quarantine broken draft parent references
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT d.id, d.folder_id, d.title, d.content, d.parent_id, d.target_type, "
                                        "d.timestamp, m.artifact_id FROM drafts d "
                                        "LEFT JOIN legacy_draft_mapping m ON m.draft_id = d.id ORDER BY d.id ASC;",
                                        -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     sqlite3_int64 draftId = sqlite3_column_int64(stmt, 0);
                     std::optional<sqlite3_int64> folderId;
                     if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) folderId = sqlite3_column_int64(stmt, 1);
                     QString title = colText(stmt, 2);
                     QString content = colText(stmt, 3);
                     std::optional<sqlite3_int64> parentId;
                     if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) parentId = sqlite3_column_int64(stmt, 4);
                     QString targetType = colText(stmt, 5);
                     QString timestamp = colText(stmt, 6);

                     if (sqlite3_column_type(stmt, 7) == SQLITE_NULL) {
                         sqlite3_int64 artId = 0;
                         if (!insertArtifactAndVersion("draft", folderId, title, content, QString(), timestamp,
                                                       artId)) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         sqlite3_stmt* dmStmt = nullptr;
                         if (sqlite3_prepare_v2(handle,
                                                "INSERT INTO legacy_draft_mapping (draft_id, artifact_id, "
                                                "legacy_parent_id, legacy_target_type) VALUES (?, ?, ?, ?);",
                                                -1, &dmStmt, nullptr) != SQLITE_OK) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         sqlite3_bind_int64(dmStmt, 1, draftId);
                         sqlite3_bind_int64(dmStmt, 2, artId);
                         if (parentId.has_value()) {
                             sqlite3_bind_int64(dmStmt, 3, *parentId);
                         } else {
                             sqlite3_bind_null(dmStmt, 3);
                         }
                         if (!targetType.isNull()) {
                             const QByteArray ttUtf8 = targetType.toUtf8();
                             sqlite3_bind_text(dmStmt, 4, ttUtf8.constData(), -1, SQLITE_TRANSIENT);
                         } else {
                             sqlite3_bind_null(dmStmt, 4);
                         }
                         bool dmOk = sqlite3_step(dmStmt) == SQLITE_DONE;
                         sqlite3_finalize(dmStmt);
                         if (!dmOk) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     } else {
                         sqlite3_int64 artId = sqlite3_column_int64(stmt, 7);
                         if (!syncMappedArtifact(artId, folderId, title, content, QString(""), timestamp)) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             // Resolve draft lineage for any unlinked drafts and quarantine drafts with missing non-zero parent_id
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(
                         handle,
                         "SELECT d.id, d.folder_id, d.title, d.content, d.parent_id, d.target_type, d.timestamp, "
                         "m.artifact_id, v.id, v.forked_from_version_id, v.is_sealed "
                         "FROM drafts d JOIN legacy_draft_mapping m ON m.draft_id = d.id "
                         "JOIN artifacts a ON a.id = m.artifact_id "
                         "JOIN artifact_versions v ON v.id = a.current_version_id ORDER BY d.id ASC;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     sqlite3_int64 draftId = sqlite3_column_int64(stmt, 0);
                     std::optional<sqlite3_int64> folderId;
                     if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) folderId = sqlite3_column_int64(stmt, 1);
                     QString title = colText(stmt, 2);
                     QString content = colText(stmt, 3);
                     std::optional<sqlite3_int64> parentId;
                     if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) parentId = sqlite3_column_int64(stmt, 4);
                     QString targetType = colText(stmt, 5);
                     QString timestamp = colText(stmt, 6);
                     sqlite3_int64 draftVerId = sqlite3_column_int64(stmt, 8);
                     bool hasForkedFrom = sqlite3_column_type(stmt, 9) != SQLITE_NULL;
                     bool draftVerSealed = sqlite3_column_int(stmt, 10) != 0;

                     if (!parentId.has_value() || *parentId <= 0 || hasForkedFrom) {
                         continue;
                     }

                     QString lookupSql;
                     if (targetType == "document") {
                         lookupSql =
                             "SELECT a.current_version_id FROM legacy_document_mapping m JOIN artifacts a ON a.id = "
                             "m.artifact_id WHERE m.document_id = ?;";
                     } else if (targetType == "note") {
                         lookupSql =
                             "SELECT a.current_version_id FROM legacy_note_mapping m JOIN artifacts a ON a.id = "
                             "m.artifact_id WHERE m.note_id = ?;";
                     } else if (targetType == "template") {
                         lookupSql =
                             "SELECT a.current_version_id FROM legacy_template_mapping m JOIN artifacts a ON a.id = "
                             "m.artifact_id WHERE m.template_id = ?;";
                     } else if (targetType == "draft") {
                         lookupSql =
                             "SELECT a.current_version_id FROM legacy_draft_mapping m JOIN artifacts a ON a.id = "
                             "m.artifact_id WHERE m.draft_id = ?;";
                     }

                     std::optional<sqlite3_int64> resolvedParentVerId;
                     if (!lookupSql.isEmpty()) {
                         sqlite3_stmt* lStmt = nullptr;
                         if (sqlite3_prepare_v2(handle, lookupSql.toUtf8().constData(), -1, &lStmt, nullptr) !=
                             SQLITE_OK) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         sqlite3_bind_int64(lStmt, 1, *parentId);
                         if (sqlite3_step(lStmt) == SQLITE_ROW) {
                             resolvedParentVerId = sqlite3_column_int64(lStmt, 0);
                         }
                         sqlite3_finalize(lStmt);
                     }

                     if (resolvedParentVerId.has_value() && !draftVerSealed) {
                         if (!db.execute(QString("UPDATE artifact_versions SET is_sealed = 1 WHERE id = %1;")
                                             .arg(*resolvedParentVerId))) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         if (!db.execute(
                                 QString("UPDATE artifact_versions SET forked_from_version_id = %1 WHERE id = %2;")
                                     .arg(*resolvedParentVerId)
                                     .arg(draftVerId))) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     } else if (!resolvedParentVerId.has_value()) {
                         QJsonObject rawObj;
                         rawObj["id"] = static_cast<qint64>(draftId);
                         rawObj["folder_id"] =
                             folderId.has_value() ? QJsonValue(static_cast<qint64>(*folderId)) : QJsonValue::Null;
                         rawObj["title"] = title.isNull() ? QJsonValue::Null : QJsonValue(title);
                         rawObj["content"] = content.isNull() ? QJsonValue::Null : QJsonValue(content);
                         rawObj["parent_id"] = static_cast<qint64>(*parentId);
                         rawObj["target_type"] = targetType.isNull() ? QJsonValue::Null : QJsonValue(targetType);
                         rawObj["timestamp"] = timestamp.isNull() ? QJsonValue::Null : QJsonValue(timestamp);
                         const QString payloadJson =
                             QString::fromUtf8(QJsonDocument(rawObj).toJson(QJsonDocument::Compact));
                         if (!insertQuarantineIfMissing("drafts", draftId,
                                                        targetType.isNull() ? QString("") : targetType, parentId, title,
                                                        content, payloadJson, "missing_parent_artifact")) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             // 5. Migrate document_history rows to sealed artifact_versions (or quarantine orphans)
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT h.id, h.document_id, h.action_type, h.content, h.timestamp, "
                                        "m.artifact_id "
                                        "FROM document_history h "
                                        "LEFT JOIN legacy_document_mapping m ON m.document_id = h.document_id "
                                        "WHERE h.id NOT IN (SELECT history_id FROM legacy_document_history_mapping) "
                                        "ORDER BY h.document_id ASC, h.timestamp ASC, h.id ASC;",
                                        -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }

                 int rc = SQLITE_OK;
                 std::optional<sqlite3_int64> currentArtId;
                 std::optional<sqlite3_int64> lastHistoryVerId;
                 QString currentArtTitle;

                 auto linkRootVersionIfMutable = [&](sqlite3_int64 artId, sqlite3_int64 tailHistoryVerId) -> bool {
                     sqlite3_stmt* rootStmt = nullptr;
                     if (sqlite3_prepare_v2(
                             handle,
                             "SELECT id, is_sealed FROM artifact_versions "
                             "WHERE artifact_id = ? AND parent_id IS NULL "
                             "AND id NOT IN (SELECT artifact_version_id FROM legacy_document_history_mapping) "
                             "ORDER BY id ASC LIMIT 1;",
                             -1, &rootStmt, nullptr) != SQLITE_OK) {
                         return false;
                     }
                     sqlite3_bind_int64(rootStmt, 1, artId);
                     int rootRc = sqlite3_step(rootStmt);
                     if (rootRc == SQLITE_ROW) {
                         sqlite3_int64 rootVerId = sqlite3_column_int64(rootStmt, 0);
                         bool rootSealed = sqlite3_column_int(rootStmt, 1) != 0;
                         sqlite3_finalize(rootStmt);
                         if (!rootSealed && rootVerId != tailHistoryVerId) {
                             if (!db.execute(QString("UPDATE artifact_versions SET parent_id = %1 WHERE id = %2;")
                                                 .arg(tailHistoryVerId)
                                                 .arg(rootVerId))) {
                                 return false;
                             }
                         }
                         return true;
                     }
                     sqlite3_finalize(rootStmt);
                     return rootRc == SQLITE_DONE;
                 };

                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     sqlite3_int64 histId = sqlite3_column_int64(stmt, 0);
                     std::optional<sqlite3_int64> docId;
                     if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) docId = sqlite3_column_int64(stmt, 1);
                     QString actionType = colText(stmt, 2);
                     QString content = colText(stmt, 3);
                     QString timestamp = colText(stmt, 4);

                     if (sqlite3_column_type(stmt, 5) == SQLITE_NULL) {
                         QJsonObject rawObj;
                         rawObj["id"] = static_cast<qint64>(histId);
                         rawObj["document_id"] =
                             docId.has_value() ? QJsonValue(static_cast<qint64>(*docId)) : QJsonValue::Null;
                         rawObj["action_type"] = actionType.isNull() ? QJsonValue::Null : QJsonValue(actionType);
                         rawObj["content"] = content.isNull() ? QJsonValue::Null : QJsonValue(content);
                         rawObj["timestamp"] = timestamp.isNull() ? QJsonValue::Null : QJsonValue(timestamp);
                         const QString payloadJson =
                             QString::fromUtf8(QJsonDocument(rawObj).toJson(QJsonDocument::Compact));
                         if (!insertQuarantineIfMissing("document_history", histId, "document", docId, actionType,
                                                        content, payloadJson, "missing_parent_document")) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         continue;
                     }

                     sqlite3_int64 artId = sqlite3_column_int64(stmt, 5);
                     if (!currentArtId.has_value() || *currentArtId != artId) {
                         if (currentArtId.has_value() && lastHistoryVerId.has_value()) {
                             if (!linkRootVersionIfMutable(*currentArtId, *lastHistoryVerId)) {
                                 sqlite3_finalize(stmt);
                                 return false;
                             }
                         }
                         currentArtId = artId;
                         lastHistoryVerId.reset();
                         currentArtTitle = QString();

                         sqlite3_stmt* tStmt = nullptr;
                         if (sqlite3_prepare_v2(handle,
                                                "SELECT v.title FROM artifacts a JOIN artifact_versions v ON v.id = "
                                                "a.current_version_id WHERE a.id = ?;",
                                                -1, &tStmt, nullptr) != SQLITE_OK) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         sqlite3_bind_int64(tStmt, 1, artId);
                         if (sqlite3_step(tStmt) == SQLITE_ROW) {
                             currentArtTitle = colText(tStmt, 0);
                         }
                         sqlite3_finalize(tStmt);

                         sqlite3_stmt* prevStmt = nullptr;
                         if (sqlite3_prepare_v2(handle,
                                                "SELECT v.id FROM legacy_document_history_mapping hm "
                                                "JOIN artifact_versions v ON v.id = hm.artifact_version_id "
                                                "WHERE v.artifact_id = ? ORDER BY hm.history_id DESC LIMIT 1;",
                                                -1, &prevStmt, nullptr) != SQLITE_OK) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         sqlite3_bind_int64(prevStmt, 1, artId);
                         if (sqlite3_step(prevStmt) == SQLITE_ROW) {
                             lastHistoryVerId = sqlite3_column_int64(prevStmt, 0);
                         }
                         sqlite3_finalize(prevStmt);
                     }

                     QJsonObject metaObj;
                     metaObj["action_type"] = actionType.isNull() ? QString("") : actionType;
                     metaObj["legacy_history_id"] = static_cast<qint64>(histId);
                     const QByteArray metaUtf8 = QJsonDocument(metaObj).toJson(QJsonDocument::Compact);

                     sqlite3_stmt* insVerStmt = nullptr;
                     if (sqlite3_prepare_v2(
                             handle,
                             "INSERT INTO artifact_versions (artifact_id, parent_id, title, content, metadata, "
                             "is_sealed, created_at) VALUES (?, ?, ?, ?, ?, 1, ?) RETURNING id;",
                             -1, &insVerStmt, nullptr) != SQLITE_OK) {
                         sqlite3_finalize(stmt);
                         return false;
                     }
                     sqlite3_bind_int64(insVerStmt, 1, artId);
                     if (lastHistoryVerId.has_value()) {
                         sqlite3_bind_int64(insVerStmt, 2, *lastHistoryVerId);
                     } else {
                         sqlite3_bind_null(insVerStmt, 2);
                     }
                     if (!currentArtTitle.isNull()) {
                         const QByteArray tUtf8 = currentArtTitle.toUtf8();
                         sqlite3_bind_text(insVerStmt, 3, tUtf8.constData(), -1, SQLITE_TRANSIENT);
                     } else {
                         sqlite3_bind_null(insVerStmt, 3);
                     }
                     if (!content.isNull()) {
                         const QByteArray cUtf8 = content.toUtf8();
                         sqlite3_bind_text(insVerStmt, 4, cUtf8.constData(), -1, SQLITE_TRANSIENT);
                     } else {
                         sqlite3_bind_null(insVerStmt, 4);
                     }
                     sqlite3_bind_text(insVerStmt, 5, metaUtf8.constData(), -1, SQLITE_TRANSIENT);
                     if (!timestamp.isNull()) {
                         const QByteArray tsUtf8 = timestamp.toUtf8();
                         sqlite3_bind_text(insVerStmt, 6, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
                     } else {
                         sqlite3_bind_null(insVerStmt, 6);
                     }

                     if (sqlite3_step(insVerStmt) != SQLITE_ROW) {
                         sqlite3_finalize(insVerStmt);
                         sqlite3_finalize(stmt);
                         return false;
                     }
                     sqlite3_int64 newHistVerId = sqlite3_column_int64(insVerStmt, 0);
                     sqlite3_finalize(insVerStmt);

                     if (!db.execute(QString("INSERT INTO legacy_document_history_mapping (history_id, "
                                             "artifact_version_id) VALUES (%1, %2);")
                                         .arg(histId)
                                         .arg(newHistVerId))) {
                         sqlite3_finalize(stmt);
                         return false;
                     }
                     lastHistoryVerId = newHistVerId;
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;

                 if (currentArtId.has_value() && lastHistoryVerId.has_value()) {
                     if (!linkRootVersionIfMutable(*currentArtId, *lastHistoryVerId)) {
                         return false;
                     }
                 }
             }

             // 6. Quarantine orphaned comments whose target entity does not exist
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT id, entity_type, entity_id, content, created_at FROM comments "
                                        "ORDER BY id ASC;",
                                        -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     sqlite3_int64 commentId = sqlite3_column_int64(stmt, 0);
                     QString entityType = colText(stmt, 1);
                     std::optional<sqlite3_int64> entityId;
                     if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) entityId = sqlite3_column_int64(stmt, 2);
                     QString content = colText(stmt, 3);
                     QString createdAt = colText(stmt, 4);

                     bool exists = false;
                     if (entityId.has_value() && *entityId > 0) {
                         QString checkSql;
                         if (entityType == "document") {
                             checkSql = QString("SELECT COUNT(*) FROM legacy_document_mapping WHERE document_id = %1;")
                                            .arg(*entityId);
                         } else if (entityType == "note") {
                             checkSql =
                                 QString("SELECT COUNT(*) FROM legacy_note_mapping WHERE note_id = %1;").arg(*entityId);
                         } else if (entityType == "template") {
                             checkSql = QString("SELECT COUNT(*) FROM legacy_template_mapping WHERE template_id = %1;")
                                            .arg(*entityId);
                         } else if (entityType == "draft") {
                             checkSql = QString("SELECT COUNT(*) FROM legacy_draft_mapping WHERE draft_id = %1;")
                                            .arg(*entityId);
                         } else if (entityType == "message") {
                             checkSql = QString("SELECT COUNT(*) FROM messages WHERE id = %1;").arg(*entityId);
                         } else if (entityType == "folder") {
                             checkSql = QString("SELECT COUNT(*) FROM folders WHERE id = %1;").arg(*entityId);
                         }
                         if (!checkSql.isEmpty()) {
                             int cnt = 0;
                             if (!db.queryInt(checkSql, cnt)) {
                                 sqlite3_finalize(stmt);
                                 return false;
                             }
                             exists = cnt > 0;
                         }
                     }

                     if (!exists) {
                         QJsonObject rawObj;
                         rawObj["id"] = static_cast<qint64>(commentId);
                         rawObj["entity_type"] = entityType.isNull() ? QJsonValue::Null : QJsonValue(entityType);
                         rawObj["entity_id"] =
                             entityId.has_value() ? QJsonValue(static_cast<qint64>(*entityId)) : QJsonValue::Null;
                         rawObj["content"] = content.isNull() ? QJsonValue::Null : QJsonValue(content);
                         rawObj["created_at"] = createdAt.isNull() ? QJsonValue::Null : QJsonValue(createdAt);
                         const QString payloadJson =
                             QString::fromUtf8(QJsonDocument(rawObj).toJson(QJsonDocument::Compact));
                         if (!insertQuarantineIfMissing("comments", commentId,
                                                        entityType.isNull() ? QString("") : entityType, entityId,
                                                        QString(), content, payloadJson, "missing_target_entity")) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             return true;
         }});

    runner.addMigration(
        {27, 28, "Unified Folders Normalization, Cycle Recovery, and Index", [](Database& db) {
             sqlite3* handle = db.handle();
             if (!handle) return false;

             // 1. Normalize folder types into canonical root-kind values
             if (!db.execute(
                     "UPDATE folders SET type = 'documents' WHERE type IS NULL OR TRIM(type) = '' OR LOWER(TRIM(type)) "
                     "IN ('folder', 'document', 'documents', 'docs_folder', 'doc_folder');"
                     "UPDATE folders SET type = 'notes' WHERE LOWER(TRIM(type)) IN ('note', 'notes', 'notes_folder');"
                     "UPDATE folders SET type = 'templates' WHERE LOWER(TRIM(type)) IN ('template', 'templates', "
                     "'templates_folder');"
                     "UPDATE folders SET type = 'drafts' WHERE LOWER(TRIM(type)) IN ('draft', 'drafts', "
                     "'drafts_folder');"
                     "UPDATE folders SET type = 'chats' WHERE LOWER(TRIM(type)) IN ('chat', 'chats', 'chats_folder', "
                     "'messages');")) {
                 return false;
             }

             auto insertQuarantineIfMissing = [&](const QString& sourceTable, sqlite3_int64 legacyId,
                                                  const QString& targetType, std::optional<sqlite3_int64> targetId,
                                                  const QString& title, const QString& content,
                                                  const QString& payloadJson, const QString& reason) -> bool {
                 const char* checkSql =
                     "SELECT id FROM quarantined_records WHERE source_table = ? AND legacy_id = ? AND reason = ? "
                     "LIMIT 1;";
                 sqlite3_stmt* checkStmt = nullptr;
                 if (sqlite3_prepare_v2(handle, checkSql, -1, &checkStmt, nullptr) != SQLITE_OK) return false;
                 const QByteArray srcUtf8 = sourceTable.toUtf8();
                 const QByteArray reasonUtf8 = reason.toUtf8();
                 sqlite3_bind_text(checkStmt, 1, srcUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_int64(checkStmt, 2, legacyId);
                 sqlite3_bind_text(checkStmt, 3, reasonUtf8.constData(), -1, SQLITE_TRANSIENT);
                 bool exists = (sqlite3_step(checkStmt) == SQLITE_ROW);
                 sqlite3_finalize(checkStmt);
                 if (exists) return true;

                 const char* insSql =
                     "INSERT INTO quarantined_records (source_table, legacy_id, target_type, parent_reference, title, "
                     "content, raw_payload_json, reason) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
                 sqlite3_stmt* insStmt = nullptr;
                 if (sqlite3_prepare_v2(handle, insSql, -1, &insStmt, nullptr) != SQLITE_OK) return false;
                 const QByteArray targetTypeUtf8 = targetType.toUtf8();
                 const QByteArray titleUtf8 = title.toUtf8();
                 const QByteArray contentUtf8 = content.toUtf8();
                 const QByteArray payloadUtf8 = payloadJson.toUtf8();
                 sqlite3_bind_text(insStmt, 1, srcUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_int64(insStmt, 2, legacyId);
                 sqlite3_bind_text(insStmt, 3, targetTypeUtf8.constData(), -1, SQLITE_TRANSIENT);
                 if (targetId.has_value()) {
                     sqlite3_bind_int64(insStmt, 4, *targetId);
                 } else {
                     sqlite3_bind_null(insStmt, 4);
                 }
                 sqlite3_bind_text(insStmt, 5, titleUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(insStmt, 6, contentUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(insStmt, 7, payloadUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(insStmt, 8, reasonUtf8.constData(), -1, SQLITE_TRANSIENT);
                 bool ok = (sqlite3_step(insStmt) == SQLITE_DONE);
                 sqlite3_finalize(insStmt);
                 return ok;
             };

             struct RawFolder {
                 int id = 0;
                 int parentId = 0;
                 QString name;
                 QString type;
             };
             QHash<int, RawFolder> folderMap;
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle, "SELECT id, parent_id, name, type FROM folders ORDER BY id ASC;", -1,
                                        &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     RawFolder rf;
                     rf.id = sqlite3_column_int(stmt, 0);
                     rf.parentId = sqlite3_column_int(stmt, 1);
                     const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                     const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                     rf.name = n ? QString::fromUtf8(n) : QString();
                     rf.type = t ? QString::fromUtf8(t) : QStringLiteral("documents");
                     folderMap.insert(rf.id, rf);
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             // 2. Detect orphaned or cyclic folders and recover them to root (parent_id = 0) while quarantining
             for (auto it = folderMap.begin(); it != folderMap.end(); ++it) {
                 const RawFolder rf = it.value();
                 if (rf.parentId <= 0) continue;

                 QString quarantineReason;
                 if (!folderMap.contains(rf.parentId)) {
                     quarantineReason = QStringLiteral("orphaned_parent_folder");
                 } else {
                     QSet<int> visited;
                     visited.insert(rf.id);
                     int curr = rf.parentId;
                     while (curr > 0 && folderMap.contains(curr)) {
                         if (visited.contains(curr)) {
                             quarantineReason = QStringLiteral("cyclic_folder_hierarchy");
                             break;
                         }
                         visited.insert(curr);
                         curr = folderMap.value(curr).parentId;
                     }
                 }

                 if (!quarantineReason.isEmpty()) {
                     QJsonObject rawObj;
                     rawObj["id"] = rf.id;
                     rawObj["parent_id"] = rf.parentId;
                     rawObj["name"] = rf.name;
                     rawObj["type"] = rf.type;
                     const QString payloadJson =
                         QString::fromUtf8(QJsonDocument(rawObj).toJson(QJsonDocument::Compact));
                     if (!insertQuarantineIfMissing("folders", rf.id, "folder", rf.parentId, rf.name, QString(),
                                                    payloadJson, quarantineReason)) {
                         return false;
                     }
                     if (!db.execute(QString("UPDATE folders SET parent_id = 0 WHERE id = %1;").arg(rf.id))) {
                         return false;
                     }
                     it.value().parentId = 0;
                 }
             }

             // 3. Recover artifacts pointing to nonexistent folders so they remain visible at root
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT a.id, a.kind, a.folder_id, v.title FROM artifacts a "
                                        "LEFT JOIN artifact_versions v ON v.id = a.current_version_id "
                                        "WHERE a.folder_id > 0 AND NOT EXISTS (SELECT 1 FROM folders f WHERE f.id = "
                                        "a.folder_id);",
                                        -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     const int artId = sqlite3_column_int(stmt, 0);
                     const char* k = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                     const int badFolderId = sqlite3_column_int(stmt, 2);
                     const char* ttl = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                     const QString kindStr = k ? QString::fromUtf8(k) : QStringLiteral("document");
                     const QString titleStr = ttl ? QString::fromUtf8(ttl) : QString();

                     QJsonObject rawObj;
                     rawObj["artifact_id"] = artId;
                     rawObj["kind"] = kindStr;
                     rawObj["folder_id"] = badFolderId;
                     rawObj["title"] = titleStr;
                     const QString payloadJson =
                         QString::fromUtf8(QJsonDocument(rawObj).toJson(QJsonDocument::Compact));
                     if (!insertQuarantineIfMissing("artifacts", artId, "folder", badFolderId, titleStr, QString(),
                                                    payloadJson, "orphaned_item_folder")) {
                         sqlite3_finalize(stmt);
                         return false;
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;

                 if (!db.execute("UPDATE artifacts SET folder_id = 0 WHERE folder_id > 0 AND NOT EXISTS (SELECT 1 FROM "
                                 "folders f WHERE f.id = artifacts.folder_id);")) {
                     return false;
                 }
             }

             return db.execute("CREATE INDEX IF NOT EXISTS idx_folders_parent_type ON folders(parent_id, type);");
         }});

    // Version 28 -> 29: Durable prompt revisions, AI runs, and run inputs (#241, #242, #243)
    runner.addMigration(
        {28, 29, "Durable Prompt Revisions and AI Runs", [](Database& db) -> bool {
             sqlite3* handle = db.handle();
             if (!handle) return false;

             if (!db.execute("CREATE TABLE IF NOT EXISTS prompt_revisions ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "source_kind TEXT NOT NULL DEFAULT 'user', "
                             "template_id TEXT DEFAULT '', "
                             "target_kind TEXT DEFAULT '', "
                             "target_id INTEGER DEFAULT 0, "
                             "system_prompt TEXT DEFAULT '', "
                             "prompt_text TEXT NOT NULL, "
                             "model TEXT DEFAULT '', "
                             "is_sealed BOOLEAN DEFAULT 0, "
                             "created_at DATETIME DEFAULT CURRENT_TIMESTAMP);")) {
                 return false;
             }

             if (!db.execute("CREATE TRIGGER IF NOT EXISTS prevent_sealed_prompt_revision_update "
                             "BEFORE UPDATE ON prompt_revisions "
                             "WHEN OLD.is_sealed = 1 "
                             "BEGIN "
                             "    SELECT RAISE(ABORT, 'Cannot modify sealed prompt_revision'); "
                             "END;")) {
                 return false;
             }

             if (!db.execute("CREATE TABLE IF NOT EXISTS ai_runs ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "parent_run_id INTEGER DEFAULT NULL, "
                             "prompt_revision_id INTEGER NOT NULL, "
                             "target_kind TEXT NOT NULL DEFAULT 'message', "
                             "target_id INTEGER DEFAULT 0, "
                             "base_version_id INTEGER DEFAULT 0, "
                             "target_action TEXT DEFAULT '', "
                             "model TEXT NOT NULL DEFAULT '', "
                             "state TEXT NOT NULL DEFAULT 'pending', "
                             "priority INTEGER DEFAULT 0, "
                             "worker_instance_id TEXT DEFAULT '', "
                             "lease_expires_at DATETIME DEFAULT NULL, "
                             "response_text TEXT DEFAULT '', "
                             "result_version_id INTEGER DEFAULT 0, "
                             "last_error TEXT DEFAULT '', "
                             "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                             "started_at DATETIME DEFAULT NULL, "
                             "finished_at DATETIME DEFAULT NULL);")) {
                 return false;
             }

             if (!db.execute("CREATE INDEX IF NOT EXISTS idx_ai_runs_state_priority "
                             "ON ai_runs(state, priority DESC, created_at ASC);")) {
                 return false;
             }

             if (!db.execute("CREATE TABLE IF NOT EXISTS ai_run_inputs ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "run_id INTEGER NOT NULL, "
                             "ordinal INTEGER NOT NULL, "
                             "artifact_id INTEGER DEFAULT 0, "
                             "version_id INTEGER DEFAULT 0, "
                             "role TEXT DEFAULT 'source', "
                             "UNIQUE(run_id, ordinal));")) {
                 return false;
             }

             auto quarantineRow = [&](const QString& srcTable, int legacyId, const QString& targetType,
                                      std::optional<int> parentRef, const QString& title, const QString& content,
                                      const QString& payloadJson, const QString& reason) -> bool {
                 const char* checkSql =
                     "SELECT 1 FROM quarantined_records WHERE source_table = ? AND legacy_id = ? AND reason = ?;";
                 sqlite3_stmt* checkStmt = nullptr;
                 if (sqlite3_prepare_v2(handle, checkSql, -1, &checkStmt, nullptr) != SQLITE_OK) return false;
                 const QByteArray srcUtf8 = srcTable.toUtf8();
                 const QByteArray reasonUtf8 = reason.toUtf8();
                 sqlite3_bind_text(checkStmt, 1, srcUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_int(checkStmt, 2, legacyId);
                 sqlite3_bind_text(checkStmt, 3, reasonUtf8.constData(), -1, SQLITE_TRANSIENT);
                 const bool exists = (sqlite3_step(checkStmt) == SQLITE_ROW);
                 sqlite3_finalize(checkStmt);
                 if (exists) return true;

                 const char* insSql =
                     "INSERT INTO quarantined_records (source_table, legacy_id, target_type, parent_reference, title, "
                     "content, raw_payload_json, reason) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
                 sqlite3_stmt* insStmt = nullptr;
                 if (sqlite3_prepare_v2(handle, insSql, -1, &insStmt, nullptr) != SQLITE_OK) return false;
                 const QByteArray tgtUtf8 = targetType.toUtf8();
                 const QByteArray ttlUtf8 = title.toUtf8();
                 const QByteArray cntUtf8 = content.toUtf8();
                 const QByteArray payUtf8 = payloadJson.toUtf8();
                 sqlite3_bind_text(insStmt, 1, srcUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_int(insStmt, 2, legacyId);
                 sqlite3_bind_text(insStmt, 3, tgtUtf8.constData(), -1, SQLITE_TRANSIENT);
                 if (parentRef.has_value()) {
                     sqlite3_bind_int(insStmt, 4, *parentRef);
                 } else {
                     sqlite3_bind_null(insStmt, 4);
                 }
                 sqlite3_bind_text(insStmt, 5, ttlUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(insStmt, 6, cntUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(insStmt, 7, payUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(insStmt, 8, reasonUtf8.constData(), -1, SQLITE_TRANSIENT);
                 const bool ok = (sqlite3_step(insStmt) == SQLITE_DONE);
                 sqlite3_finalize(insStmt);
                 return ok;
             };

             auto insertPromptRevision = [&](const QString& sourceKind, const QString& targetKind, int targetId,
                                             const QString& promptText, const QString& model,
                                             const QString& createdAt) -> int {
                 const char* sql =
                     "INSERT INTO prompt_revisions "
                     "(source_kind, template_id, target_kind, target_id, system_prompt, prompt_text, model, "
                     "is_sealed, created_at) VALUES (?, '', ?, ?, '', ?, ?, 1, COALESCE(NULLIF(?, ''), "
                     "CURRENT_TIMESTAMP)) RETURNING id;";
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;
                 const QByteArray skUtf8 = sourceKind.toUtf8();
                 const QByteArray tkUtf8 = targetKind.toUtf8();
                 const QByteArray ptUtf8 = promptText.toUtf8();
                 const QByteArray mUtf8 = model.toUtf8();
                 const QByteArray tsUtf8 = createdAt.toUtf8();
                 sqlite3_bind_text(stmt, 1, skUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(stmt, 2, tkUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_int(stmt, 3, targetId);
                 sqlite3_bind_text(stmt, 4, ptUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(stmt, 5, mUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(stmt, 6, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
                 int id = -1;
                 if (sqlite3_step(stmt) == SQLITE_ROW) {
                     id = sqlite3_column_int(stmt, 0);
                 }
                 sqlite3_finalize(stmt);
                 return id;
             };

             // 1. Migrate prompt_history -> prompt_revisions
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT id, document_id, prompt, model, timestamp FROM prompt_history "
                                        "ORDER BY id ASC;",
                                        -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     const int phId = sqlite3_column_int(stmt, 0);
                     const int docId = sqlite3_column_int(stmt, 1);
                     const bool isNullPrompt = (sqlite3_column_type(stmt, 2) == SQLITE_NULL);
                     const char* pTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                     const char* mTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                     const char* tsTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                     const QString promptStr = pTxt ? QString::fromUtf8(pTxt) : QString();
                     const QString modelStr = mTxt ? QString::fromUtf8(mTxt) : QString();
                     const QString tsStr = tsTxt ? QString::fromUtf8(tsTxt) : QString();

                     if (isNullPrompt) {
                         QJsonObject raw;
                         raw["id"] = phId;
                         raw["document_id"] = docId;
                         raw["model"] = modelStr;
                         if (!quarantineRow("prompt_history", phId, "document", docId, QString(), QString(),
                                            QString::fromUtf8(QJsonDocument(raw).toJson(QJsonDocument::Compact)),
                                            "null_prompt")) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         continue;
                     }
                     if (insertPromptRevision("migrated", "document", docId, promptStr, modelStr, tsStr) <= 0) {
                         sqlite3_finalize(stmt);
                         return false;
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             // 2. Migrate queue -> prompt_revisions + ai_runs
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(
                         handle,
                         "SELECT id, message_id, model, prompt, processing_id, last_error, priority, created_at, "
                         "target_type, state, response, parent_id, target_action FROM queue ORDER BY id ASC;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     const int qId = sqlite3_column_int(stmt, 0);
                     const int msgId = sqlite3_column_int(stmt, 1);
                     const char* mTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                     const bool isNullPrompt = (sqlite3_column_type(stmt, 3) == SQLITE_NULL);
                     const char* pTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                     const int procId = sqlite3_column_int(stmt, 4);
                     const char* errTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
                     const int priority = sqlite3_column_int(stmt, 6);
                     const char* tsTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
                     const char* ttTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
                     const char* stTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
                     const char* respTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
                     const char* actTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));

                     const QString modelStr = mTxt ? QString::fromUtf8(mTxt) : QString();
                     const QString promptStr = pTxt ? QString::fromUtf8(pTxt) : QString();
                     const QString errStr = errTxt ? QString::fromUtf8(errTxt) : QString();
                     const QString tsStr = tsTxt ? QString::fromUtf8(tsTxt) : QString();
                     const QString targetTypeStr =
                         ttTxt && *ttTxt ? QString::fromUtf8(ttTxt) : QStringLiteral("message");
                     const QString legacyState = stTxt ? QString::fromUtf8(stTxt) : QStringLiteral("pending");
                     const QString respStr = respTxt ? QString::fromUtf8(respTxt) : QString();
                     const QString actStr = actTxt ? QString::fromUtf8(actTxt) : QString();

                     if (isNullPrompt ||
                         (targetTypeStr != QLatin1String("message") && targetTypeStr != QLatin1String("document") &&
                          targetTypeStr != QLatin1String("note") && targetTypeStr != QLatin1String("merge"))) {
                         QJsonObject raw;
                         raw["id"] = qId;
                         raw["message_id"] = msgId;
                         raw["target_type"] = targetTypeStr;
                         raw["model"] = modelStr;
                         if (!quarantineRow("queue", qId, targetTypeStr, msgId, QString(), promptStr,
                                            QString::fromUtf8(QJsonDocument(raw).toJson(QJsonDocument::Compact)),
                                            isNullPrompt ? QStringLiteral("null_prompt")
                                                         : QStringLiteral("invalid_target_type"))) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         continue;
                     }

                     const int revId =
                         insertPromptRevision("migrated", targetTypeStr, msgId, promptStr, modelStr, tsStr);
                     if (revId <= 0) {
                         sqlite3_finalize(stmt);
                         return false;
                     }

                     QString runState = QStringLiteral("pending");
                     QString finalErr = errStr;
                     if (legacyState == QLatin1String("completed")) {
                         runState = QStringLiteral("completed");
                     } else if (legacyState == QLatin1String("error") || !errStr.isEmpty()) {
                         runState = QStringLiteral("failed");
                     } else if (legacyState == QLatin1String("processing") || procId > 0) {
                         runState = QStringLiteral("pending");
                         if (finalErr.isEmpty()) {
                             finalErr = QStringLiteral("Recovered interrupted queue item during v29 migration");
                         }
                     }

                     const char* insRunSql =
                         "INSERT INTO ai_runs "
                         "(id, prompt_revision_id, target_kind, target_id, target_action, model, state, priority, "
                         "response_text, last_error, created_at) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, COALESCE(NULLIF(?, ''), CURRENT_TIMESTAMP));";
                     sqlite3_stmt* runStmt = nullptr;
                     if (sqlite3_prepare_v2(handle, insRunSql, -1, &runStmt, nullptr) != SQLITE_OK) {
                         sqlite3_finalize(stmt);
                         return false;
                     }
                     const QByteArray tkUtf8 = targetTypeStr.toUtf8();
                     const QByteArray actUtf8 = actStr.toUtf8();
                     const QByteArray modUtf8 = modelStr.toUtf8();
                     const QByteArray stUtf8 = runState.toUtf8();
                     const QByteArray rUtf8 = respStr.toUtf8();
                     const QByteArray eUtf8 = finalErr.toUtf8();
                     const QByteArray tsUtf8 = tsStr.toUtf8();
                     sqlite3_bind_int(runStmt, 1, qId);
                     sqlite3_bind_int(runStmt, 2, revId);
                     sqlite3_bind_text(runStmt, 3, tkUtf8.constData(), -1, SQLITE_TRANSIENT);
                     sqlite3_bind_int(runStmt, 4, msgId);
                     sqlite3_bind_text(runStmt, 5, actUtf8.constData(), -1, SQLITE_TRANSIENT);
                     sqlite3_bind_text(runStmt, 6, modUtf8.constData(), -1, SQLITE_TRANSIENT);
                     sqlite3_bind_text(runStmt, 7, stUtf8.constData(), -1, SQLITE_TRANSIENT);
                     sqlite3_bind_int(runStmt, 8, priority);
                     sqlite3_bind_text(runStmt, 9, rUtf8.constData(), -1, SQLITE_TRANSIENT);
                     sqlite3_bind_text(runStmt, 10, eUtf8.constData(), -1, SQLITE_TRANSIENT);
                     sqlite3_bind_text(runStmt, 11, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
                     const bool ok = (sqlite3_step(runStmt) == SQLITE_DONE);
                     sqlite3_finalize(runStmt);
                     if (!ok) {
                         sqlite3_finalize(stmt);
                         return false;
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             // 3. Migrate document_merges -> prompt_revisions + ai_runs + ai_run_inputs
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT id, document_id, source_document_ids, timestamp, version_history_id "
                                        "FROM document_merges ORDER BY id ASC;",
                                        -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     const int dmId = sqlite3_column_int(stmt, 0);
                     const int docId = sqlite3_column_int(stmt, 1);
                     const char* srcIdsTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
                     const char* tsTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                     const QString srcIdsStr = srcIdsTxt ? QString::fromUtf8(srcIdsTxt) : QString();
                     const QString tsStr = tsTxt ? QString::fromUtf8(tsTxt) : QString();

                     const QStringList parts = srcIdsStr.split(',', Qt::SkipEmptyParts);
                     QList<int> parsedIds;
                     bool allValid = !parts.isEmpty();
                     for (const QString& p : parts) {
                         bool ok = false;
                         const int val = p.trimmed().toInt(&ok);
                         if (!ok || val <= 0) {
                             allValid = false;
                             break;
                         }
                         parsedIds.append(val);
                     }

                     if (!allValid) {
                         QJsonObject raw;
                         raw["id"] = dmId;
                         raw["document_id"] = docId;
                         raw["source_document_ids"] = srcIdsStr;
                         if (!quarantineRow("document_merges", dmId, "document", docId, QString(), srcIdsStr,
                                            QString::fromUtf8(QJsonDocument(raw).toJson(QJsonDocument::Compact)),
                                            "unparseable_merge_sources")) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         continue;
                     }

                     const int revId = insertPromptRevision("migrated", "merge", docId,
                                                            QStringLiteral("Migrated merge run"), QString(), tsStr);
                     if (revId <= 0) {
                         sqlite3_finalize(stmt);
                         return false;
                     }

                     const char* insMergeRunSql =
                         "INSERT INTO ai_runs (prompt_revision_id, target_kind, target_id, target_action, model, "
                         "state, created_at) VALUES (?, 'merge', ?, 'merge', '', 'completed', "
                         "COALESCE(NULLIF(?, ''), CURRENT_TIMESTAMP)) RETURNING id;";
                     sqlite3_stmt* mrStmt = nullptr;
                     if (sqlite3_prepare_v2(handle, insMergeRunSql, -1, &mrStmt, nullptr) != SQLITE_OK) {
                         sqlite3_finalize(stmt);
                         return false;
                     }
                     const QByteArray tsUtf8 = tsStr.toUtf8();
                     sqlite3_bind_int(mrStmt, 1, revId);
                     sqlite3_bind_int(mrStmt, 2, docId);
                     sqlite3_bind_text(mrStmt, 3, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
                     int runId = -1;
                     if (sqlite3_step(mrStmt) == SQLITE_ROW) {
                         runId = sqlite3_column_int(mrStmt, 0);
                     }
                     sqlite3_finalize(mrStmt);
                     if (runId <= 0) {
                         sqlite3_finalize(stmt);
                         return false;
                     }

                     for (int ord = 0; ord < parsedIds.size(); ++ord) {
                         int artId = parsedIds.at(ord);
                         sqlite3_stmt* mapStmt = nullptr;
                         if (sqlite3_prepare_v2(
                                 handle, "SELECT artifact_id FROM legacy_document_mapping WHERE document_id = ?;", -1,
                                 &mapStmt, nullptr) == SQLITE_OK) {
                             sqlite3_bind_int(mapStmt, 1, artId);
                             if (sqlite3_step(mapStmt) == SQLITE_ROW) {
                                 artId = sqlite3_column_int(mapStmt, 0);
                             }
                             sqlite3_finalize(mapStmt);
                         }

                         sqlite3_stmt* inStmt = nullptr;
                         if (sqlite3_prepare_v2(handle,
                                                "INSERT INTO ai_run_inputs (run_id, ordinal, artifact_id, version_id, "
                                                "role) VALUES (?, ?, ?, 0, 'source');",
                                                -1, &inStmt, nullptr) != SQLITE_OK) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                         sqlite3_bind_int(inStmt, 1, runId);
                         sqlite3_bind_int(inStmt, 2, ord);
                         sqlite3_bind_int(inStmt, 3, artId);
                         const bool ok = (sqlite3_step(inStmt) == SQLITE_DONE);
                         sqlite3_finalize(inStmt);
                         if (!ok) {
                             sqlite3_finalize(stmt);
                             return false;
                         }
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             return true;
         }});

    // Version 29 -> 30: Explicit chat sessions, message chat_id/run_id provenance, and orphan recovery (#247, #239)
    runner.addMigration(
        {29, 30, "Explicit Chat Sessions and Message Provenance", [](Database& db) -> bool {
             sqlite3* handle = db.handle();
             if (!handle) return false;

             if (!db.execute("CREATE TABLE IF NOT EXISTS chat_sessions ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                             "root_message_id INTEGER DEFAULT 0, "
                             "folder_id INTEGER DEFAULT 0, "
                             "title TEXT DEFAULT '', "
                             "system_prompt TEXT DEFAULT '', "
                             "model TEXT DEFAULT '', "
                             "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, "
                             "updated_at DATETIME DEFAULT CURRENT_TIMESTAMP);")) {
                 return false;
             }

             if (!db.execute("CREATE INDEX IF NOT EXISTS idx_chat_sessions_root "
                             "ON chat_sessions(root_message_id);")) {
                 return false;
             }

             bool hasChatId = false;
             if (!db.hasColumn("messages", "chat_id", hasChatId)) return false;
             if (!hasChatId && !db.execute("ALTER TABLE messages ADD COLUMN chat_id INTEGER DEFAULT 0;")) {
                 return false;
             }

             bool hasRunId = false;
             if (!db.hasColumn("messages", "run_id", hasRunId)) return false;
             if (!hasRunId && !db.execute("ALTER TABLE messages ADD COLUMN run_id INTEGER DEFAULT 0;")) {
                 return false;
             }

             // 1. Quarantine and recover orphaned messages whose parent_id points to a missing message
             {
                 sqlite3_stmt* orphStmt = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "SELECT m.id, m.parent_id, m.role, m.content FROM messages m "
                                        "WHERE m.parent_id IS NOT NULL AND m.parent_id > 0 "
                                        "AND NOT EXISTS (SELECT 1 FROM messages p WHERE p.id = m.parent_id);",
                                        -1, &orphStmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(orphStmt)) == SQLITE_ROW) {
                     const int msgId = sqlite3_column_int(orphStmt, 0);
                     const int badParent = sqlite3_column_int(orphStmt, 1);
                     const char* roleTxt = reinterpret_cast<const char*>(sqlite3_column_text(orphStmt, 2));
                     const char* cntTxt = reinterpret_cast<const char*>(sqlite3_column_text(orphStmt, 3));
                     const QString roleStr = roleTxt ? QString::fromUtf8(roleTxt) : QString();
                     const QString cntStr = cntTxt ? QString::fromUtf8(cntTxt) : QString();

                     QJsonObject raw;
                     raw["id"] = msgId;
                     raw["parent_id"] = badParent;
                     raw["role"] = roleStr;

                     sqlite3_stmt* qStmt = nullptr;
                     if (sqlite3_prepare_v2(
                             handle,
                             "INSERT INTO quarantined_records (source_table, legacy_id, target_type, parent_reference, "
                             "title, content, raw_payload_json, reason) VALUES ('messages', ?, 'chat', ?, '', ?, ?, "
                             "'orphaned_parent_message');",
                             -1, &qStmt, nullptr) != SQLITE_OK) {
                         sqlite3_finalize(orphStmt);
                         return false;
                     }
                     const QByteArray cUtf8 = cntStr.toUtf8();
                     const QByteArray pUtf8 = QJsonDocument(raw).toJson(QJsonDocument::Compact);
                     sqlite3_bind_int(qStmt, 1, msgId);
                     sqlite3_bind_int(qStmt, 2, badParent);
                     sqlite3_bind_text(qStmt, 3, cUtf8.constData(), -1, SQLITE_TRANSIENT);
                     sqlite3_bind_text(qStmt, 4, pUtf8.constData(), -1, SQLITE_TRANSIENT);
                     const bool ok = (sqlite3_step(qStmt) == SQLITE_DONE);
                     sqlite3_finalize(qStmt);
                     if (!ok) {
                         sqlite3_finalize(orphStmt);
                         return false;
                     }
                 }
                 sqlite3_finalize(orphStmt);
                 if (rc != SQLITE_DONE) return false;

                 if (!db.execute("UPDATE messages SET parent_id = 0 "
                                 "WHERE parent_id IS NOT NULL AND parent_id > 0 "
                                 "AND NOT EXISTS (SELECT 1 FROM messages p WHERE p.id = messages.parent_id);")) {
                     return false;
                 }
             }

             // 2. Backfill chat_sessions for all root messages and propagate chat_id to descendants
             struct MsgNode {
                 int id = 0;
                 int parentId = 0;
                 int folderId = 0;
                 QString content;
                 QString timestamp;
             };
             QHash<int, MsgNode> msgMap;
             QList<MsgNode> roots;
             {
                 sqlite3_stmt* stmt = nullptr;
                 if (sqlite3_prepare_v2(
                         handle,
                         "SELECT id, COALESCE(parent_id, 0), COALESCE(folder_id, 0), COALESCE(content, ''), "
                         "COALESCE(timestamp, '') FROM messages ORDER BY id ASC;",
                         -1, &stmt, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 int rc = SQLITE_OK;
                 while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                     MsgNode n;
                     n.id = sqlite3_column_int(stmt, 0);
                     n.parentId = sqlite3_column_int(stmt, 1);
                     n.folderId = sqlite3_column_int(stmt, 2);
                     const char* cTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
                     const char* tsTxt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
                     n.content = cTxt ? QString::fromUtf8(cTxt) : QString();
                     n.timestamp = tsTxt ? QString::fromUtf8(tsTxt) : QString();
                     msgMap.insert(n.id, n);
                     if (n.parentId == 0) {
                         roots.append(n);
                     }
                 }
                 sqlite3_finalize(stmt);
                 if (rc != SQLITE_DONE) return false;
             }

             QHash<int, int> rootToChatId;
             for (const MsgNode& root : roots) {
                 // Check if a chat_session already exists for this root
                 int existingChatId = 0;
                 {
                     sqlite3_stmt* chk = nullptr;
                     if (sqlite3_prepare_v2(handle, "SELECT id FROM chat_sessions WHERE root_message_id = ?;", -1, &chk,
                                            nullptr) == SQLITE_OK) {
                         sqlite3_bind_int(chk, 1, root.id);
                         if (sqlite3_step(chk) == SQLITE_ROW) {
                             existingChatId = sqlite3_column_int(chk, 0);
                         }
                         sqlite3_finalize(chk);
                     }
                 }
                 if (existingChatId > 0) {
                     rootToChatId.insert(root.id, existingChatId);
                     continue;
                 }

                 QString title;
                 QString sysPrompt;
                 QString model;
                 {
                     sqlite3_stmt* cStmt = nullptr;
                     if (sqlite3_prepare_v2(handle,
                                            "SELECT COALESCE(title, ''), COALESCE(systemPrompt, ''), "
                                            "COALESCE(model, '') FROM chats WHERE message_id = ?;",
                                            -1, &cStmt, nullptr) == SQLITE_OK) {
                         sqlite3_bind_int(cStmt, 1, root.id);
                         if (sqlite3_step(cStmt) == SQLITE_ROW) {
                             const char* t = reinterpret_cast<const char*>(sqlite3_column_text(cStmt, 0));
                             const char* s = reinterpret_cast<const char*>(sqlite3_column_text(cStmt, 1));
                             const char* m = reinterpret_cast<const char*>(sqlite3_column_text(cStmt, 2));
                             title = t ? QString::fromUtf8(t) : QString();
                             sysPrompt = s ? QString::fromUtf8(s) : QString();
                             model = m ? QString::fromUtf8(m) : QString();
                         }
                         sqlite3_finalize(cStmt);
                     }
                 }
                 if (title.isEmpty()) {
                     title = root.content.trimmed().left(60);
                 }

                 sqlite3_stmt* ins = nullptr;
                 if (sqlite3_prepare_v2(handle,
                                        "INSERT INTO chat_sessions (root_message_id, folder_id, title, system_prompt, "
                                        "model, created_at, updated_at) VALUES (?, ?, ?, ?, ?, "
                                        "COALESCE(NULLIF(?, ''), CURRENT_TIMESTAMP), "
                                        "COALESCE(NULLIF(?, ''), CURRENT_TIMESTAMP)) RETURNING id;",
                                        -1, &ins, nullptr) != SQLITE_OK) {
                     return false;
                 }
                 const QByteArray tUtf8 = title.toUtf8();
                 const QByteArray sUtf8 = sysPrompt.toUtf8();
                 const QByteArray mUtf8 = model.toUtf8();
                 const QByteArray tsUtf8 = root.timestamp.toUtf8();
                 sqlite3_bind_int(ins, 1, root.id);
                 sqlite3_bind_int(ins, 2, root.folderId);
                 sqlite3_bind_text(ins, 3, tUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(ins, 4, sUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(ins, 5, mUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(ins, 6, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
                 sqlite3_bind_text(ins, 7, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
                 int chatId = 0;
                 if (sqlite3_step(ins) == SQLITE_ROW) {
                     chatId = sqlite3_column_int(ins, 0);
                 }
                 sqlite3_finalize(ins);
                 if (chatId <= 0) return false;
                 rootToChatId.insert(root.id, chatId);
             }

             // Propagate chat_id to all messages
             for (auto it = msgMap.begin(); it != msgMap.end(); ++it) {
                 int curr = it.key();
                 QSet<int> visited;
                 while (msgMap.contains(curr) && msgMap.value(curr).parentId > 0 && !visited.contains(curr)) {
                     visited.insert(curr);
                     curr = msgMap.value(curr).parentId;
                 }
                 const int chatId = rootToChatId.value(curr, 0);
                 if (chatId > 0) {
                     if (!db.execute(
                             QString("UPDATE messages SET chat_id = %1 WHERE id = %2;").arg(chatId).arg(it.key()))) {
                         return false;
                     }
                 }
             }

             return true;
         }});

    // Version 30 -> 31: Upgrade notifications for ActivityStore (#246, #234)
    runner.addMigration({30, 31, "Activity Notifications Schema Upgrade", [](Database& db) -> bool {
                             if (!db.handle()) return false;

                             auto addColIfMissing = [&db](const QString& col, const QString& def) -> bool {
                                 bool exists = false;
                                 if (!db.hasColumn("notifications", col, exists)) return false;
                                 if (exists) return true;
                                 return db.execute(
                                     QString("ALTER TABLE notifications ADD COLUMN %1 %2;").arg(col, def));
                             };

                             if (!addColIfMissing("run_id", "INTEGER DEFAULT 0")) return false;
                             if (!addColIfMissing("severity", "TEXT DEFAULT 'info'")) return false;
                             if (!addColIfMissing("title", "TEXT DEFAULT ''")) return false;
                             if (!addColIfMissing("message", "TEXT DEFAULT ''")) return false;
                             if (!addColIfMissing("dismissed_at", "DATETIME DEFAULT NULL")) return false;

                             return db.execute(
                                 "CREATE INDEX IF NOT EXISTS idx_notifications_dismissed "
                                 "ON notifications(is_dismissed, created_at DESC);");
                         }});

    return runner;
}

}  // namespace db
