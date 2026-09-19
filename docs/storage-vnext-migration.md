# Storage vNext Migration Plan

Tracking issues: #238, #239, #245

This document describes how KLlamaBooks should move from the current schema (v21 at the time this was written) to the storage-vNext model without silently losing durable user data.

The migration must be implemented and tested as a transactional application migration, not as a collection of best-effort `ALTER TABLE` statements.

## Migration contract

For every durable legacy datum, one of the following must be true after migration:

1. it maps to an explicit vNext entity/version/relation;
2. it is preserved losslessly in recovery/quarantine storage with an explanation of why an automatic mapping was unsafe; or
3. it belongs to an explicitly disposable category documented below.

At present the only deliberately disposable legacy category is **notification rows**. Their underlying durable domain state (content, prompts, runs/errors/results where recoverable) is not disposable.

## Current state wins over historical inference

The migration must never decide current user-visible content by choosing the newest history row.

For each live document/note/template/draft row, migrate the content/title/metadata that the current table actually contains as the initial current vNext state first. Historical rows are then attached as prior/legacy versions when their relation can be established.

The essential invariant is:

```text
legacy current user-visible content
    ==
vNext selected/current artifact version content
```

byte-for-byte unless an explicit encoding normalization is proven safe and separately tested.

## Pre-migration safety

For file-backed Books:

- verify that the source database can be read successfully;
- create/retain a recoverable pre-migration copy or backup strategy before destructive table replacement;
- acquire the migration lock/transaction before transforming schema state;
- do not advance the schema version until validation passes.

For in-memory/test databases, use the same migration code path without requiring a filesystem backup.

## Migration runner

Introduce an explicit migration framework that:

- knows the current and target schema versions;
- executes ordered steps;
- returns checked errors with migration-step context;
- runs the cutover transactionally where SQLite permits it;
- rolls back on failure;
- causes `BookDatabase::open()`/its successor to fail if migration did not complete;
- validates the result before committing/declaring success.

Do not rely on ignored return values from individual `sqlite3_exec()` calls.

## Suggested cutover phases

These are implementation phases, not permanent dual-write modes.

### Phase 1: create vNext structures

Create the new schema under final or temporary names inside the migration transaction. Enable foreign-key enforcement and required indexes/constraints.

### Phase 2: migrate current artifact-like state

Import current rows from:

- `documents`;
- `notes`;
- `templates`;
- `drafts`.

Create stable artifact identities and the version/draft state required to reproduce what the user currently sees.

Legacy drafts that can be associated with an original should preserve that relation. Ambiguous/unassociated drafts remain valid standalone migrated/recovered content rather than being dropped.

### Phase 3: migrate artifact history and lineage

Import `document_history` and existing document-parent/lineage metadata as prior/legacy versions when the mapping is safe.

Do not assume history timestamps imply canonical current order when the legacy data disagrees with the live row.

Where the old history lacks enough information to prove a relation, preserve the row in recovery storage or mark imported lineage as incomplete rather than inventing it.

### Phase 4: migrate prompts and AI execution history

Import recoverable state from:

- legacy `queue` rows;
- `prompt_history`;
- prompt/model metadata embedded in documents/settings/JSON;
- existing generation errors/results/state where durable.

The migration should create sealed prompt/run records only where the association is supportable from stored evidence.

If prompt history points to a deleted queue row, retain the prompt/history as legacy provenance rather than treating the missing queue row as proof of a specific completion state.

### Phase 5: migrate merges/provenance

Import legacy merge rows and source-document metadata.

When only source artifact IDs are known, record that the historical source **version** is unknown. Do not claim that the artifact's current version was necessarily the one used historically.

Preserve the original prompt/model/source identity and any recoverable output/history. Ambiguous relations go to recovery storage.

### Phase 6: migrate chats/messages/settings

Preserve every message body and parent relationship.

Introduce explicit chat identity while retaining branching semantics. Migrate titles/settings/drafts/user notes to the appropriate chat/message override ownership without altering message content.

The migration must include fixtures where unrelated legacy tables share the same numeric IDs to prove typed mapping is used.

### Phase 7: migrate folders/navigation metadata

Preserve folder hierarchy, names, kind/space, ordering and meaningful expanded state.

Map legacy typed folder roots into the unified container model while retaining the default separated Chats/Documents/Notes/Templates/Drafts experience.

### Phase 8: comments/settings/other durable metadata

Map comments and meaningful settings to their new typed owners.

For arbitrary JSON metadata:

- extract relational/provenance fields into proper relations where known;
- retain non-relational options/metadata in an appropriate versioned/options payload;
- quarantine unknown durable fields rather than silently discarding them if they cannot be preserved in place.

### Phase 9: disposable notifications

Legacy notification rows may be dropped. AI activity in the new design is derived from run/domain state, while future arbitrary notifications use the new notification/event mechanism.

### Phase 10: validate, switch, retire legacy structures

Before commit/completion:

- `PRAGMA foreign_key_check` must return no rows;
- `PRAGMA integrity_check` must return `ok`;
- every migrated live artifact has a selected/current state;
- every sealed durable reference points to an allowed sealed version;
- every migrated run has internally consistent status/prompt/input/output relations;
- chat parent relations remain within the correct chat;
- current content checksums/body comparisons match legacy current rows;
- recovery-item counts/reasons are recorded and testable.

Only after successful validation should schema version advance and production code treat the vNext schema as active.

Do not leave a permanent dual-write path to v21 tables.

## Recovery/quarantine rules

Use recovery storage when:

- a legacy row contains durable user content but its intended owner is ambiguous;
- an entity relation cannot be proven because the legacy schema only stored an untyped ID;
- a historical merge source version cannot be identified;
- malformed metadata can be preserved but not safely interpreted;
- an old invariant is already broken and automatically choosing a repair could attach content to the wrong entity.

A recovery item should retain enough raw data to reconstruct/export the original information even if no automated recovery action is implemented yet.

Do **not** use recovery storage for:

- ordinary new foreign-key violations (fail the transaction instead);
- normal deletion of an artifact whose referenced historical content remains preserved through regular version/run relations;
- transient runtime/network failures;
- disposable legacy notifications.

## Help-menu recovery UX

The post-migration Help menu should expose an Orphaned / Recovered Data view.

For each item, show:

- source category/table/key;
- migration reason;
- readable content where available;
- raw metadata/payload on demand;
- likely/candidate targets where useful but not automatically safe;
- status (unresolved, recreated/relinked, exported, deliberately deleted, etc.).

Supported actions should include:

- export/extract;
- recreate/import as a suitable artifact where possible;
- reassign/relink where the relation is well-defined;
- deliberately delete after confirmation.

Recovery actions should be transactional and must not silently replace existing canonical content.

## Required migration test fixtures

At minimum include fixtures for:

- ordinary current document/note/template content;
- current content that disagrees with the newest history row;
- several historical document entries;
- multiple saved drafts including an unassociated/ambiguous draft;
- legacy queue rows in pending/running/error/completed-like combinations;
- prompt history whose queue row still exists and whose queue row is absent;
- merge rows with source IDs and prompt/model data;
- chats with nested forks, inherited/custom titles, draft prompts and notes;
- deliberate numeric ID collisions across entity types;
- malformed/ambiguous metadata that must be quarantined;
- migration-step failure to prove rollback;
- a large-enough fixture to catch accidental O(N^2) migration behavior where practical.

## Data-loss policy for implementation PRs

A migration PR must not respond to an awkward legacy case by deleting it, replacing it with an empty/default row, or choosing an arbitrary relation merely to make tests pass.

If the code cannot safely map a durable row, add/extend the recovery representation and a fixture explaining the case.