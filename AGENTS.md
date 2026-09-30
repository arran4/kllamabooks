We don't need ui testing or network based testing.
Never disable github workflows unless specified.

## Code Structure & Ordering

To maintain consistency and reduce merge conflicts, please follow this ordering for class members in `.cpp` files:

1.  **Includes** (grouped by library/module)
2.  **Constants / Static Helpers**
3.  **Constructor / Destructor**
4.  **Public Methods**
5.  **Slots** (grouped by functionality: Tray, List, Toolbar, etc.)
6.  **Private Helpers** (Setup, Logic)

In header files (`.h`), group declarations similarly and use comments to separate sections.

## Never Nest Principle

Avoid deep nesting of `if/else` blocks. Use guard clauses (early returns) to handle edge cases and error conditions first. This makes the "happy path" of the function less indented and easier to read.

## Function Size & Complexity

Break down large functions into smaller, single-purpose helper functions. This improves readability and makes the code easier to test and maintain.

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
Prefer deterministic model/database tests rather than UI/network tests.

## Storage vNext Architecture (TARGET STATE)

The storage-vNext work tracked by #238 defines the replacement architecture. The authoritative design is documented in `docs/storage-vnext-architecture.md` and `docs/storage-vnext-migration.md`.

### Mutable Drafts to Sealed References
Artifacts and prompts are not immutable from birth. User edits update a mutable draft. Explicitly finishing an edit or using it in a durable AI run seals it transactionally. Once sealed, a version is immutable and further edits create a new mutable descendant.

### Unified Artifacts and Folders
Documents, notes, templates, and drafts use common artifact/version primitives. Folder/container persistence is also unified, while preserving distinct visual roots/spaces. Core mutation code uses typed item references instead of raw IDs.

### Durable AI Runs
The AI operation/run is the durable lifecycle object. It retains the exact prompt, model, options, inputs, output, and execution state. Queueing is merely a run state, not a separate duplicated history system.

### Migration and Recovery
Storage-vNext migration must preserve durable user data safely. If legacy data cannot be mapped accurately, it is preserved in an explicit recovery/quarantine storage, exposed via a Help menu, rather than being discarded or guessed.

## Current Application Architecture Learnings (CURRENT STATE)

- **Current Implementation State:** The repository is at schema version 24. The checked/ordered migration runner (#250) is in place. The initial `ArtifactStore` API with `artifacts` and `artifact_versions` exists (#253). A backfill migration (22->23) for legacy documents is implemented (#255), and database-level immutability for sealed artifact versions is enforced (23->24) (#256).
- **Remaining Storage vNext Work:** Production document/note/template/draft UI and CRUD paths still use the legacy persistence layer. Notes, templates, and saved drafts are not yet migrated onto `ArtifactStore`. The broader artifact lifecycle (#240) and the no-loss migration logic (#239) are still incomplete.
- **Books & Databases:** The main application manages "Books" which represent individual encrypted SQLite databases (`BookDatabase`).
- **UI Components:** The main view utilizes `QSplitter`s. The left pane shows "Open Books" and "Closed Books". The right pane is a `QStackedWidget` switching between a linear chat view and a full branching chat tree.
- **State Management:** Backend/domain state is authoritative. Never use UI state (like checking text content or widget properties) to determine internal logic state.
- **Document Versioning:** Modifying AI documents defaults to creating a new nested sub-document.
- **Current AI Interaction & Queueing (Legacy):** Document AI operations (e.g., text completion, rewriting) are currently dispatched via `QueueManager` using the legacy `queue` table, and sometimes `OllamaClient::generate` is called synchronously in the UI thread for isolated single-document text tasks. *This is a transitional legacy state, pending the complete migration to the durable AI-runs architecture (vNext).*
- **Robust Error Notifications:** For UI error handling around non-interactive components, utilize internal application status bars rather than intrusive `QMessageBox` pop-ups unless the user is actively blocked. Ensure missing local SQLite databases show a prompt to the user and don't crash.
