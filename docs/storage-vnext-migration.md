# Storage vNext Migration Plan

Tracking issues: #238, #239, #245

This document describes how KLlamaBooks should move from the current schema to the storage-vNext model without silently losing durable user data.

## Migration contract

For every durable legacy datum, one of the following must be true after migration:
1. it maps to an explicit vNext entity/version/relation;
2. it is preserved losslessly in recovery/quarantine storage with an explanation of why an automatic mapping was unsafe; or
3. it belongs to an explicitly disposable category (e.g., disposable legacy notifications).

## Current state wins over historical inference

The migration must never decide current user-visible content by choosing the newest history row. For each live document/note/template/draft row, migrate the content/title/metadata that the current table actually contains as the initial current vNext state first.

## Migration runner

The migration must be implemented and tested as a transactional application migration. It must execute ordered steps, return checked errors, run transactionally where SQLite permits, roll back on failure, and cause database open to fail cleanly if migration fails. (Note: The foundational runner from #250 is already implemented in `main`).

## Recovery / quarantine rules

Use recovery storage when:
- a legacy row contains durable user content but its intended owner is ambiguous;
- an entity relation cannot be proven because the legacy schema only stored an untyped ID;
- a historical merge source version cannot be identified;
- malformed metadata can be preserved but not safely interpreted;
- an old invariant is already broken and automatically choosing a repair could attach content to the wrong entity.

A recovery item should retain enough raw data to reconstruct/export the original information.

Do **not** use recovery storage for ordinary new foreign-key violations, normal deletion of an artifact whose history is preserved, transient network failures, or disposable notifications.

## Help-menu recovery UX

The post-migration Help menu should expose an Orphaned / Recovered Data view, allowing users to inspect, export, recreate/import, relink, or deliberately delete recovered data.

## Data-loss policy for implementation PRs

A migration PR must not respond to an awkward legacy case by deleting it, replacing it with an empty/default row, or choosing an arbitrary relation merely to make tests pass. If the code cannot safely map a durable row, add/extend the recovery representation and a fixture explaining the case.
