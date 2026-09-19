We don't need UI testing or network based testing.
Never disable GitHub workflows unless specified.

## Code Structure & Ordering

To maintain consistency and reduce merge conflicts, follow this ordering for class members in `.cpp` files:

1. **Includes** (grouped by library/module)
2. **Constants / Static Helpers**
3. **Constructor / Destructor**
4. **Public Methods**
5. **Slots** (grouped by functionality: Tray, List, Toolbar, etc.)
6. **Private Helpers** (Setup, Logic)

In header files (`.h`), group declarations similarly and use comments to separate sections.

## Never Nest Principle

Avoid deep nesting of `if/else` blocks. Use guard clauses (early returns) to handle edge cases and error conditions first. This makes the happy path less indented and easier to reason about.

## Function Size & Complexity

Break large functions into smaller, single-purpose helpers. Move persistence/business-state transitions out of widget handlers when possible so they can be tested without UI automation.

## Building and Compiling (Qt6 / KF6 Migration)

This project is currently migrating to Qt6 and KDE Frameworks 6 (KF6).
Docker must not be used for the development/test environment.
`.jules/bootstrap.sh` provisions the shared KDE development rootfs.
Build and test commands that require KDE/Qt dependencies must run through `.jules/run.sh`.

For example:

```bash
.jules/run.sh cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
.jules/run.sh cmake --build build --parallel
.jules/run.sh ctest --test-dir build --output-on-failure
```

Prefer deterministic database/model tests over UI automation and live network tests. Use fakes for LLM/provider behavior.

## Storage vNext Architecture

The current production schema is still the legacy schema while the storage-vNext work tracked by #238 is implemented. Do not extend legacy queue/history/merge/document-like persistence with new parallel sources of truth when the vNext issues already define the replacement. Small correctness fixes against current `main` remain valid, but new architecture work should follow the vNext design.

The authoritative design is documented in `docs/storage-vnext-architecture.md` and `docs/storage-vnext-migration.md` and tracked through #238 and its child issues (#239-#248).

### Editable then sealed

Artifacts and prompts are **not immutable from birth**.

- While the user is actively editing, state may exist as a mutable draft/version candidate.
- Autosave and passive navigation should update/preserve that mutable draft rather than create an immutable revision on every keystroke.
- Explicitly finishing/committing an edit seals that version.
- Before any durable object references a version (especially an AI run, merge input, fork, restore/provenance relation), seal it transactionally first.
- Once sealed, a version is immutable. Further edits create a new mutable descendant based on it.
- Never update a sealed version in place.

If a user navigates away from dirty work without explicitly committing it, preserve it as a draft/working version. Do not silently turn navigation into destructive save or discard.

### Unified artifacts and folders

Documents, notes, templates and saved working drafts should use common artifact/version primitives rather than independent CRUD implementations. Kind-specific behavior remains explicit, but common operations (load, title, content, folder, move, copy, history, lineage) should not require separate table dispatch in UI code.

Folder/container persistence should also be unified. The default UX must continue to show distinct Chats, Documents, Notes, Templates and Drafts roots/spaces. The model must additionally support an optional unified-navigation view over the same underlying data; do not duplicate content to implement that view.

Use typed item/container references in core logic. Avoid passing raw integer IDs plus ad-hoc strings such as `"document"` or `"chat_node"` through domain operations.

### Prompts

Prompts follow the same mutable-to-sealed lifecycle.

- A prompt can be edited freely while it is a draft.
- Creating a durable AI run seals the exact prompt state used.
- Exact rerun references the original sealed prompt.
- Editing a historical prompt creates a descendant prompt/version; it does not rewrite the earlier run's provenance.
- Reusable operations/templates must be snapshotted/versioned sufficiently that changing today's template cannot alter yesterday's run history.

### Durable AI runs are the execution source of truth

An AI operation/run is the durable lifecycle object. Queueing is a run state/query, not a separate disposable history system.

A run should retain the exact prompt, request-local system prompt/options, model/provider/endpoint identity, inputs, output, error, target/base version, review/apply state and retry/regenerate lineage needed to explain and reproduce it.

- Retry/regenerate creates a new descendant run; never rewrite the original run.
- Cancel changes run state; do not delete history merely to stop execution.
- Successful output remains durable even if the user rejects applying it to a document.
- Applying AI output to an artifact is an explicit transactional transition that creates/seals the appropriate artifact version.
- Conflict detection must compare expected/base version identity, not timestamps or UI text.
- Generation state must come from backend run state, never placeholder strings/widgets.

Use a worker-instance lease/expiry model for crash recovery and atomic claiming. Do not treat internal processing counters as OS process IDs.

### Request-local LLM/provider configuration

Never store mutable per-request generation configuration globally on a shared client. Model, system prompt and request options belong to the request/run and must be passed together so concurrent generations cannot affect each other.

Keep provider-neutral seams compatible with #235 while preserving Ollama as the only required provider until separately expanded.

### Merges

A merge is a normal AI run with multiple ordered inputs, not an independent execution lifecycle.

Each input must identify/snapshot the exact sealed source version used. Exact rerun uses those same versions. "Rerun with latest sources" resolves the source artifact identities again and creates a new run with new input-version provenance.

Do not store relational source IDs as comma-separated strings or only inside JSON when real relations can represent them.

### Activity and notifications

AI pending/running/error/review/completion activity should normally be derived from durable run state. Do not create duplicate notification rows that merely mirror a run and then require another lookup to recover the details.

Keep a small separate arbitrary-notification/event mechanism for future notifications that are genuinely not reducible to domain state.

Background progress/failures should use status/activity/notification UI rather than intrusive modal dialogs. Use modal dialogs for destructive confirmation, unresolved data conflicts, and guided validation where user action is required.

### Migration and orphan recovery

Storage-vNext migration must preserve durable user data. Current live rows are authoritative for current user-visible content; never infer current content from whichever history row happens to be newest.

- Migration must be transactional and checked. `open()` must fail cleanly if migration fails.
- Do not dual-write old and new schemas indefinitely.
- Validate with `PRAGMA foreign_key_check`, `PRAGMA integrity_check`, and application invariants before committing the migration.
- If durable legacy data cannot be mapped safely, preserve it losslessly in recovery/quarantine storage rather than guessing or dropping it.
- The Help menu will expose Orphaned / Recovered Data so the user can inspect, export, recreate/reassign where supported, or deliberately delete it.
- Legacy notification rows are allowed to be discarded during this cutover; underlying durable content/runs/prompts are not.

New-schema invariant violations should fail the transaction. Do not use the orphan-recovery table as normal runtime control flow.

### Persistence/service boundaries

Prefer a small set of typed stores/domain operations (for example ArtifactStore, ChatStore, PromptStore, RunStore plus checked transaction/statement helpers) over adding more raw CRUD to `BookDatabase`.

UI code should call atomic intent-level operations such as save/finalize artifact, seal prompt + create run, retry run, apply result, or copy/fork chat. It should not manually sequence half-transactions such as writing history, updating current content, creating a notification and deleting a queue row.

Use scoped enums/value types for core kinds/states/actions and centralize serialization at persistence boundaries. Avoid generic table-name mutation APIs from UI code.

## Current Application Architecture Learnings

- **Books & Databases:** The application manages Books, each represented by an encrypted SQLCipher SQLite database.
- **UI Components:** The main view uses splitters. The left side presents open books and their content plus closed book files; the right side switches among VFS/navigation, document/note views and branching chat views.
- **Drag and Drop:** Existing drag/drop is coordinated from `MainWindow::eventFilter`; storage-vNext navigation work should move canonical mutation/state ownership out of duplicated widget models as described by #223/#244.
- **Chat:** Conversations are branching message trees. Storage-vNext #247 makes chat identity explicit and separates it from message/leaf/title-owner identity. Preserve branch semantics while removing overloaded IDs.
- **Document Editing:** Documents use source editing plus Markdown preview. Draft/editor lifecycle should follow #229/#240: preserve dirty working state, make commit/finalize explicit, and use sealed versions for referenced history.
- **Document AI & Queueing:** Current `main` routes document AI through `QueueManager`/the legacy `queue` table. This is a transitional implementation. New work should target durable runs (#242), not add more queue-specific lifecycle state.
- **Templates / AI Operations:** Existing built-in/global/book operations and templates remain user-facing concepts, but execution provenance must follow #241/#233 so old runs are not changed by later template edits.
- **State Checks:** Never use UI state, placeholder text or widget properties as authority for internal generation/data state. Query backend/domain state.

## Comments and Documentation

Comments should explain non-obvious intent, invariants, ownership, state transitions and reasons for unusual behavior. Avoid generic generated comments that merely say a function "manages component initialization" or "ensures side effects map accurately".

When implementation changes an architectural decision documented here or in `docs/storage-vnext-architecture.md`, update the documentation in the same PR.