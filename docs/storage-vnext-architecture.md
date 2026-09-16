# Storage vNext Architecture

Tracking epic: #238

This document records the intended storage/domain model for the KLlamaBooks restructuring. It is deliberately more authoritative than historical comments in the current v21 implementation. Exact table/column names may change during implementation, but the invariants and user-visible semantics below should not.

## Why this cutover exists

The current application has accumulated several overlapping sources of truth:

- current document content plus separately-maintained document history;
- queue rows that also act as prompt history, output storage, error state, review state and execution state;
- a separate prompt-history table;
- merge-specific rows plus JSON/comma-separated source metadata;
- notification rows that mirror queue/run state;
- documents, notes, templates and drafts with largely parallel CRUD paths;
- chat identity, active/leaf message identity, title ownership and queue targeting encoded through overlapping message IDs/settings.

Those overlaps make ordinary operations depend on callers remembering to update several pieces of state in the correct order. Storage vNext replaces that with explicit lifecycle objects and transactional domain operations.

## Design principles

1. **Mutable while drafting, sealed when referenced.** User editing should be natural and autosavable. Immutability starts when state becomes durable provenance, not on the first keystroke.
2. **A durable reference freezes what it references.** If an AI run, fork, merge, restore or other durable relation depends on a version, that version is sealed first.
3. **History is lineage, not duplicated side data.** A committed/sealed version points to its base/parent version; restore/fork creates another version rather than rewriting history.
4. **The AI run is the execution lifecycle.** Queueing, running, retrying, failing, completing and reviewing are states/relations of the run.
5. **Relational provenance is relational.** Do not encode entity relations only as comma-separated text or opaque JSON.
6. **Current state is explicit.** Do not infer canonical state from timestamps, the latest history row, placeholder text, widget state or error-column side effects.
7. **No silent migration loss.** Durable data is mapped or quarantined for recovery. Do not fabricate provenance that the old schema never stored.
8. **Keep the UI distinctions users understand.** Normalized storage does not require collapsing Documents, Notes, Templates, Chats and Drafts into one visual bucket.

## Lifecycle: draft -> sealed -> new draft

### Artifact/version lifecycle

An artifact is a stable identity (for example a document, note or template). The artifact can have one or more working/draft versions.

A version candidate may be mutable while the user is editing it. Autosave updates this working state rather than generating a sealed revision for every edit.

A candidate becomes sealed when:

- the user explicitly commits/finalizes it (for example Save/Done);
- an AI run uses it as input;
- a merge uses it as an input;
- another durable version is forked/restored/derived from it;
- another durable relation requires reproducible provenance.

Sealing must be transactional with creation of the durable reference that required it.

After sealing, that row/version is never mutated. Starting another edit creates a new mutable descendant whose base/parent is the sealed version.

Passive navigation or closing an editor with dirty work should preserve a draft/working version. It should not silently mean "commit this as final" and it should not discard the work.

### Prompt lifecycle

Prompts use the same lifecycle semantics.

A prompt can remain mutable while the user is composing/refining it. Creating an AI run seals the exact prompt state used. Editing an already-used prompt produces a descendant prompt/version.

This provides two distinct operations:

- **Retry/rerun exactly:** use the original sealed prompt and inputs.
- **Edit and rerun:** create a descendant prompt/version and a descendant run.

Reusable prompt templates/AI operations are definitions, not historical execution state. A run must retain/snapshot the exact effective template/prompt state it used so later template edits do not change historical provenance.

## Conceptual data model

The following is a target shape, not a promise of exact names.

### Artifacts

`artifacts`

- stable artifact ID;
- kind (`document`, `note`, `template`, plus any later compatible kind);
- container/folder relation;
- selected/current version relation;
- soft-delete/archive state if needed;
- creation timestamps.

`artifact_versions`

- version ID and artifact ID;
- base/parent version relation;
- lifecycle state (`draft`/`sealed` or equivalent) and seal timestamp;
- title/content/metadata that belongs to this historical state;
- creation/update timestamps for draft state;
- optional originating AI-run relation for AI-produced/applied versions;
- change reason/kind (manual, import, AI, merge, restore, fork, migration, etc.).

The persistence layer must prevent a base/current relation from accidentally pointing at a version belonging to another artifact.

A restore creates a new descendant version containing/restoring the earlier state. It does not move the head backwards in a way that erases the later history.

### Saved drafts / working copies

"Draft" should be a lifecycle/view concept rather than a separate duplicate document persistence implementation.

The model must support multiple persisted working versions where useful. A user-facing Drafts collection may present mutable working copies across artifact kinds while still using the same artifact/version primitives.

Legacy standalone `drafts` records are migrated losslessly into this model. If a legacy draft cannot be associated safely with an original, it remains a valid standalone recovered artifact/working state rather than being discarded.

### Prompts

Prompts need stable identity/lineage plus version state analogous to artifacts. Implementation may share generic versioning primitives with artifacts or use a focused prompt table/store, provided the lifecycle invariants remain the same.

A sealed executed prompt must retain enough state to reproduce/explain the request:

- effective/rendered prompt text;
- resolved system prompt;
- source operation/template identity where applicable;
- snapshot/version of reusable template state;
- variables/user inputs used during rendering;
- parent prompt/version when derived by editing.

Model/provider/request parameters belong to the run where they are execution-specific.

### AI runs

`ai_runs` is the durable execution record.

Representative fields/relations:

- run ID;
- parent run ID for retry/regenerate lineage;
- optional batch/sibling group for multi-model execution;
- typed operation (`chat`, `generate`, `rewrite`, `complete`, `merge`, custom, etc.);
- sealed prompt/version reference;
- provider/endpoint/model/request options;
- status (`queued`, `running`, `succeeded`, `failed`, `cancelled` or equivalent);
- priority;
- worker lease owner/expiry;
- target artifact/chat/message context;
- base artifact version for optimistic apply;
- apply mode and review state;
- output content/snapshot;
- resulting artifact version or chat message relation, when applied/materialized;
- error details;
- created/started/finished/acknowledged timestamps.

The queue is a query over runnable/active run status. There is no separate queue-history ownership model.

Cancelling a run changes state. It does not erase it.

Retry/regenerate creates a new run linked to the previous run. It does not mutate the previous prompt/output/error/history.

### Run inputs

`ai_run_inputs` (or equivalent) stores ordered input provenance.

For an artifact input it should retain:

- run ID + position;
- exact sealed artifact-version relation;
- exact text/content snapshot or durable content reference sufficient to remain reproducible if the source artifact is later removed from normal navigation;
- role/label/context metadata required by the prompt renderer.

For chat/message/tool inputs, use equally explicit typed relations/snapshots.

### Merges

A merge is an AI run with multiple ordered inputs.

The current merge UX remains valid: select sources, render a merge prompt/template, select models, generate, review, inspect sources, regenerate.

Historical merge state is represented by run lineage, run inputs and artifact versions rather than a separate `document_merges` lifecycle.

Two rerun modes are required:

- **Exact:** same sealed prompt and same input versions.
- **Latest sources:** resolve the source artifact identities again, seal the versions being used, and create a new run/input set.

Multiple selected models create sibling/related runs over the same sealed prompt/input set. They should not require cloning document metadata merely to represent execution state.

### Chats and messages

Chats become explicit stable identities.

A message belongs to a chat and may have a parent message, preserving the existing branching conversation semantics.

The model must explicitly distinguish:

- chat identity;
- root message;
- selected/active/leaf message;
- branch/message settings overrides;
- display title ownership;
- AI run identity.

Sending a user message creates/persists the user message and then creates AI run(s). Normal execution should not create an empty assistant message merely to have something for the queue to target.

On successful completion the assistant message is materialized from the run output and linked to the run. With multiple models, successful assistant outputs are sibling branches under the same user message.

A failed run therefore does not leave an unexplained blank assistant message.

### Folders / containers

Folder persistence is unified structurally.

The default UI remains separated into logical roots/spaces:

- Chats
- Documents
- Notes
- Templates
- Drafts/working copies

The storage/query model must also support an optional unified explorer mode that presents compatible kinds together over the same underlying records.

Unified navigation is a view choice, not a second storage structure.

Core mutation/navigation code uses typed item/container references. Raw numeric IDs are never assumed globally unique across kinds unless the final schema deliberately provides a global identity namespace.

### Activity and notifications

AI activity is derived from run state:

- queued/running;
- failed;
- needs review;
- completed/unacknowledged;
- historical/acknowledged.

Do not write duplicate notification rows for these states merely to rediscover the run later.

Keep a small separate arbitrary-notification model for future events that are genuinely not represented by authoritative domain state. It should support category, title/body/payload, created/acknowledged state and optional typed action/navigation target.

Legacy notification rows may be discarded during the storage-vNext migration.

### Recovery / quarantine

`recovery_items` (name TBD) preserves durable legacy data that cannot be mapped safely.

A recovery item should retain:

- source schema/table/category;
- original key;
- reason mapping was unsafe/ambiguous;
- lossless payload;
- migration/schema version;
- candidate targets, if useful but not safe to choose automatically;
- resolution/deletion state.

The Help menu exposes an Orphaned / Recovered Data browser where the user can inspect, export, recreate/re-import, relink/reassign where supported, or deliberately delete recovered data.

New-schema constraint violations are errors and transaction failures, not normal reasons to dump data into recovery storage.

## Applying AI output to artifacts

AI output is not automatically canonical document content merely because generation succeeded.

A document run records the artifact and exact base version used to create it. When applying the result, the domain operation compares the artifact's current/base identity with the run's expected base.

If unchanged, apply can atomically create/seal the new version and advance current state.

If changed, the operation returns a conflict. The UI can then offer appropriate choices such as view differences, fork result, save as draft, or deliberately apply/rebase using a separately-defined operation.

Do not detect this conflict from history timestamps.

## Execution leases and crash recovery

Each application/executor instance gets a unique worker identity. Claiming a queued run atomically marks it running with a lease owner and expiry.

A live worker renews the lease. A run whose lease expired can be recovered/requeued according to explicit policy.

Do not use internal processing counters as operating-system PIDs and do not inspect `/proc` to decide whether a run belongs to a live executor.

## Provider/client requests

Generation APIs receive an immutable/request-local request object containing the model, resolved system prompt, user/effective prompt, options and provider-specific configuration needed for that request.

Do not mutate a shared `OllamaClient` system prompt immediately before issuing concurrent requests.

The provider seam should be compatible with #235 while keeping Ollama as the only required backend for this cutover.

## Domain/store boundaries

The storage cutover is also a C++ simplification opportunity.

Prefer a small bounded set of stores/services such as:

- database/transaction/statement helpers;
- ArtifactStore;
- ChatStore;
- PromptStore;
- RunStore;
- migration/recovery helpers.

A temporary `BookDatabase` facade may remain while callers migrate. Do not keep adding unrelated new responsibilities to it.

Widgets/controllers call atomic intent-level domain operations. They should not manually perform multi-step persistence sequences that can partially succeed.

Core kinds/states/actions use typed enums/value objects with serialization centralized at the persistence boundary.

## Existing issue reconciliation

Storage-vNext work must account for all currently-open issues, not create a disconnected replacement project:

- #222: immediate chat-rename corruption is fixed independently; vNext chat identity makes this distinction structural.
- #223: canonical VFS/tree state should use unified folders + typed references.
- #224: generation state comes from AI runs.
- #225: KWallet credential migration remains orthogonal; run/provider config must consume credential references safely.
- #226: regression coverage should become invariant/migration/run lifecycle coverage.
- #227: chat-copy/fork semantics move behind explicit ChatStore/domain operations.
- #228: typed item references are a core vNext requirement.
- #229: document/draft lifecycle becomes the mutable-to-sealed version lifecycle.
- #230: typed stores/direct lookup replace full-table scans.
- #231: MainWindow decomposition should consume domain/store APIs rather than invent duplicate state owners.
- #232: stale guidance/comments are addressed by architecture docs/AGENTS updates and later source-comment cleanup.
- #233: template source/purpose model integrates with prompt/template snapshots.
- #234: feedback policy consumes derived run activity + arbitrary notifications.
- #235: provider seam aligns with request-local run execution.
- #236: search indexes/returns typed artifact/chat references on top of the new model.

## Completion definition

The architecture transition is complete when production code has one authoritative lifecycle for artifacts/prompts/runs, all current user-visible durable state has migrated or been placed in explicit recovery storage, the old queue/history/merge/document-like duplicate ownership paths are retired, and regression tests enforce the invariants above.