# Storage vNext Architecture

Tracking epic: #238

This document records the intended storage/domain model for the KLlamaBooks restructuring. Exact table/column names may change during implementation, but the invariants and user-visible semantics below should not.

## Why this cutover exists

The current application has accumulated several overlapping sources of truth:
- current document content plus separately-maintained document history;
- queue rows that also act as prompt history, output storage, error state, review state, and execution state;
- a separate prompt-history table;
- merge-specific rows plus JSON/comma-separated source metadata;
- notification rows that mirror queue/run state;
- documents, notes, templates, and drafts with largely parallel CRUD paths;
- chat identity, active/leaf message identity, title ownership, and queue targeting encoded through overlapping message IDs/settings.

Storage vNext replaces that with explicit lifecycle objects and transactional domain operations.

## Design principles

1. **Mutable while drafting, sealed when referenced.** User editing should be natural and autosavable. Immutability starts when state becomes durable provenance, not on the first keystroke.
2. **A durable reference freezes what it references.** If an AI run, fork, merge, restore or other durable relation depends on a version, that version is sealed first.
3. **History is lineage, not duplicated side data.** A committed/sealed version points to its base/parent version; restore/fork creates another version rather than rewriting history.
4. **The AI run is the execution lifecycle.** Queueing, running, retrying, failing, completing and reviewing are states/relations of the run.
5. **Relational provenance is relational.** Do not encode entity relations only as comma-separated text or opaque JSON.
6. **Current state is explicit.** Do not infer canonical state from timestamps, the latest history row, placeholder text, widget state or error-column side effects.
7. **No silent migration loss.** Durable data is mapped or quarantined for recovery.
8. **Keep the UI distinctions users understand.** Normalized storage does not require collapsing Documents, Notes, Templates, Chats and Drafts into one visual bucket.

## Lifecycle: draft -> sealed -> new draft

### Artifact/version lifecycle

An artifact is a stable identity (for example a document, note or template). The artifact can have one or more working/draft versions.

A version candidate may be mutable while the user is editing it. Autosave updates this working state rather than generating a sealed revision for every edit.

A candidate becomes sealed when:
- the user explicitly commits/finalizes it;
- an AI run uses it as input;
- a merge uses it as an input;
- another durable version is forked/restored/derived from it;
- another durable relation requires reproducible provenance.

Sealing must be transactional with the creation of the durable reference that required it. After sealing, that row/version is never mutated. Starting another edit creates a new mutable descendant based on the sealed version.

### Prompt lifecycle

Prompts follow the same lifecycle semantics. Creating an AI run seals the exact prompt state used. Editing an already-used prompt produces a descendant prompt/version. Reusable prompt templates/AI operations are definitions, not historical execution state.

## Conceptual data model

### Artifacts and Unified Folders

Documents, notes, templates, and drafts use common artifact/version primitives. Folder/container persistence is also unified, while preserving distinct visual roots/spaces. Core mutation code uses typed item references instead of raw IDs.

### AI runs

`ai_runs` is the durable execution record. It retains the exact prompt, model, options, inputs, output, and execution state. Queueing is merely a run state, not a separate duplicated history system.

### Merges

A merge is an AI run with multiple ordered inputs. The historical merge state is represented by run lineage, run inputs, and artifact versions rather than a separate lifecycle.

## Execution leases and crash recovery

Each application/executor instance gets a unique worker identity. Claiming a queued run atomically marks it running with a lease owner and expiry.

## Provider/client requests

Generation APIs receive an immutable/request-local request object containing the model, resolved system prompt, user/effective prompt, options, and provider-specific configuration. Never store mutable per-request generation configuration globally on a shared client.
