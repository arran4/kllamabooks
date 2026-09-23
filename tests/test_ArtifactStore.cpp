#include <sqlcipher/sqlite3.h>

#include <QCoreApplication>
#include <QtTest>

#include "../src/db/Database.h"
#include "../src/db/MigrationFactory.h"
#include "../src/db/Migrations.h"
#include "../src/store/ArtifactStore.h"

class TestArtifactStore : public QObject {
    Q_OBJECT

   private:
    sqlite3* m_dbHandle = nullptr;
    db::Database* m_db = nullptr;
    store::ArtifactStore* m_store = nullptr;

   private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("TestOrg");
        QCoreApplication::setApplicationName("TestApp");
    }

    void init() {
        QVERIFY(sqlite3_open(":memory:", &m_dbHandle) == SQLITE_OK);
        m_db = new db::Database(m_dbHandle);

        QVERIFY(m_db->execute("PRAGMA foreign_keys = ON;"));

        int fkEnabled = 0;
        QVERIFY(m_db->queryInt("PRAGMA foreign_keys;", fkEnabled));
        QCOMPARE(fkEnabled, 1);

        db::MigrationRunner runner = db::MigrationFactory::createRunner();
        QString error;
        QVERIFY2(runner.run(*m_db, &error), qPrintable(error));

        m_store = new store::ArtifactStore(*m_db);
    }

    void testDirectSqlFkEnforcement() {
        auto artResult = m_store->createArtifact(store::ArtifactKind::Document, 1, "T", "C", "");
        QVERIFY(artResult.isSuccess());

        // Test cross-artifact parent_id
        auto art2Result = m_store->createArtifact(store::ArtifactKind::Document, 1, "T2", "C2", "");
        QVERIFY(art2Result.isSuccess());

        // Ensure you can't create a descendant referencing a non-existent base version
        auto failResult = m_store->createMutableDescendant(9999, "T", "C", "");
        QVERIFY(!failResult.isSuccess());

        // parent_id FK relies on composite (parent_id, artifact_id)
        QString invalidParentSql =
            QString("INSERT INTO artifact_versions (artifact_id, parent_id, title) VALUES (%1, %2, 'title')")
                .arg(art2Result.value->artifactId)
                .arg(artResult.value->id);
        QVERIFY(!m_db->execute(invalidParentSql));

        // Test non-existent forked_from_version_id
        QString invalidForkSql =
            QString(
                "INSERT INTO artifact_versions (artifact_id, forked_from_version_id, title) VALUES (%1, 9999, 'title')")
                .arg(art2Result.value->artifactId);
        QVERIFY(!m_db->execute(invalidForkSql));

        // Test invalid INSERT on artifacts where current_version_id != 0
        QVERIFY(!m_db->execute("INSERT INTO artifacts (kind, current_version_id) VALUES ('document', 9999)"));

        // Test invalid UPDATE on artifacts where current_version_id belongs to another artifact
        QString invalidUpdateSql = QString("UPDATE artifacts SET current_version_id = %1 WHERE id = %2")
                                       .arg(artResult.value->id)
                                       .arg(art2Result.value->artifactId);
        QVERIFY(!m_db->execute(invalidUpdateSql));
    }

    void cleanup() {
        delete m_store;
        delete m_db;
        sqlite3_close(m_dbHandle);
    }

    void testCreateMutableVersion() {
        auto result = m_store->createArtifact(store::ArtifactKind::Document, 1, "Title", "Content", "{}");
        QVERIFY(result.isSuccess());

        auto version = result.value.value();
        QCOMPARE(version.title, QString("Title"));
        QCOMPARE(version.content, QString("Content"));
        QCOMPARE(version.isSealed, false);

        auto artifact = m_store->getArtifact(version.artifactId);
        QVERIFY(artifact.has_value());
        QCOMPARE(artifact->currentVersionId, version.id);
        QCOMPARE(artifact->kind, store::ArtifactKind::Document);
    }

    void testEditMutableVersion() {
        auto createResult = m_store->createArtifact(store::ArtifactKind::Note, 0, "Initial", "Text", "");
        QVERIFY(createResult.isSuccess());
        int versionId = createResult.value->id;

        auto editResult = m_store->editVersion(versionId, "Updated Title", "Updated Content", "meta");
        QVERIFY(editResult.isSuccess());

        auto version = m_store->getVersion(versionId);
        QCOMPARE(version->title, QString("Updated Title"));
        QCOMPARE(version->content, QString("Updated Content"));
        QCOMPARE(version->metadata, QString("meta"));
    }

    void testSealVersion() {
        auto createResult = m_store->createArtifact(store::ArtifactKind::Template, 0, "Tpl", "Cont", "");
        QVERIFY(createResult.isSuccess());
        int versionId = createResult.value->id;

        auto sealResult = m_store->sealVersion(versionId);
        QVERIFY(sealResult.isSuccess());
        QCOMPARE(sealResult.value->isSealed, true);

        // Sealing an already sealed version should succeed and return the version
        auto sealAgainResult = m_store->sealVersion(versionId);
        QVERIFY(sealAgainResult.isSuccess());
    }

    void testRejectSealedMutation() {
        auto createResult = m_store->createArtifact(store::ArtifactKind::Document, 0, "Doc", "Cont", "");
        int versionId = createResult.value->id;

        m_store->sealVersion(versionId);

        auto editResult = m_store->editVersion(versionId, "New Title", "New Cont", "");
        QVERIFY(!editResult.isSuccess());
        QCOMPARE(editResult.error.value(), store::TransitionError::Sealed);
    }

    void testCreateMutableDescendant() {
        auto createResult = m_store->createArtifact(store::ArtifactKind::Document, 0, "Doc", "Cont", "");
        int baseVersionId = createResult.value->id;
        int artifactId = createResult.value->artifactId;

        // Should reject if base is not sealed
        auto failResult = m_store->createMutableDescendant(baseVersionId, "New Doc", "New Cont", "");
        QVERIFY(!failResult.isSuccess());
        QCOMPARE(failResult.error.value(), store::TransitionError::Conflict);

        m_store->sealVersion(baseVersionId);

        auto descResult = m_store->createMutableDescendant(baseVersionId, "New Doc", "New Cont", "");
        QVERIFY(descResult.isSuccess());

        auto newVersion = descResult.value.value();
        QCOMPARE(newVersion.parentId, std::optional<int>(baseVersionId));
        QVERIFY(!newVersion.forkedFromVersionId.has_value());
        QCOMPARE(newVersion.artifactId, artifactId);
        QCOMPARE(newVersion.isSealed, false);

        auto artifact = m_store->getArtifact(artifactId);
        QCOMPARE(artifact->currentVersionId, newVersion.id);
    }

    void testExplicitUserVisibleVersion() {
        auto createResult = m_store->createArtifact(store::ArtifactKind::Note, 0, "V1", "V1 Content", "");
        int v1Id = createResult.value->id;
        int artifactId = createResult.value->artifactId;

        m_store->sealVersion(v1Id);
        auto descResult = m_store->createMutableDescendant(v1Id, "V2", "V2 Content", "");
        int v2Id = descResult.value->id;

        auto currentVersion = m_store->getCurrentVersion(artifactId);
        QVERIFY(currentVersion.has_value());
        QCOMPARE(currentVersion->id, v2Id);
        QCOMPARE(currentVersion->title, QString("V2"));
    }

    void testRejectInvalidCrossReferences() {
        auto art1 = m_store->createArtifact(store::ArtifactKind::Document, 0, "Doc 1", "Cont 1", "");
        auto art2 = m_store->createArtifact(store::ArtifactKind::Document, 0, "Doc 2", "Cont 2", "");

        m_store->sealVersion(art1.value->id);
        m_store->sealVersion(art2.value->id);

        // Attempting to restore a version from a different artifact should fail
        auto restoreResult = m_store->restoreVersion(art1.value->id, art2.value->id);
        QVERIFY(!restoreResult.isSuccess());
        QCOMPARE(restoreResult.error.value(), store::TransitionError::InvalidCrossReference);
    }

    void testRestoreVersion() {
        auto artResult = m_store->createArtifact(store::ArtifactKind::Document, 0, "V1", "V1 Content", "");
        int v1Id = artResult.value->id;
        int artifactId = artResult.value->artifactId;

        m_store->sealVersion(v1Id);
        auto v2Result = m_store->createMutableDescendant(v1Id, "V2", "V2 Content", "");
        int v2Id = v2Result.value->id;

        m_store->sealVersion(v2Id);

        auto restoreResult = m_store->restoreVersion(v2Id, v1Id);
        QVERIFY(restoreResult.isSuccess());

        auto restoredVersion = restoreResult.value.value();
        QCOMPARE(restoredVersion.title, QString("V1"));
        QCOMPARE(restoredVersion.content, QString("V1 Content"));
        QCOMPARE(restoredVersion.parentId, std::optional<int>(v2Id));  // descends from current head (v2)
        QCOMPARE(restoredVersion.artifactId, artifactId);
        QCOMPARE(restoredVersion.isSealed, false);

        auto currentVersion = m_store->getCurrentVersion(artifactId);
        QCOMPARE(currentVersion->id, restoredVersion.id);
    }

    void testForkArtifact() {
        auto artResult = m_store->createArtifact(store::ArtifactKind::Document, 10, "Base", "Base Content", "meta");
        int baseVersionId = artResult.value->id;
        int baseArtifactId = artResult.value->artifactId;

        m_store->sealVersion(baseVersionId);

        auto forkResult = m_store->forkArtifact(baseVersionId, 20);
        QVERIFY(forkResult.isSuccess());

        auto forkedVersion = forkResult.value.value();
        QVERIFY(forkedVersion.artifactId != baseArtifactId);  // Must be a new artifact
        QVERIFY(!forkedVersion.parentId.has_value());         // Must not be a same-artifact parent
        QCOMPARE(forkedVersion.forkedFromVersionId,
                 std::optional<int>(baseVersionId));  // Lineage preserved explicitly across artifacts
        QCOMPARE(forkedVersion.title, QString("Base"));
        QCOMPARE(forkedVersion.content, QString("Base Content"));
        QCOMPARE(forkedVersion.metadata, QString("meta"));
        QCOMPARE(forkedVersion.isSealed, false);

        auto forkedArtifact = m_store->getArtifact(forkedVersion.artifactId);
        QVERIFY(forkedArtifact.has_value());
        QCOMPARE(forkedArtifact->folderId, 20);
        QCOMPARE(forkedArtifact->kind, store::ArtifactKind::Document);
    }
};

QTEST_MAIN(TestArtifactStore)
#include "test_ArtifactStore.moc"
