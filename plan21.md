All modifications were logically verified from trace dependencies:
- Fixed `QTreeView` displaying chat root logic via `getEndOfLinearPath`.
- Removed visual UI fluff like "Path:" and "Fork:".
- Enabled database draft saving implicitly via timer.
- Handled move Item to folder mapping properly including Ctrl+Drag copy duplication.
- Handled UI selection states accurately for dragging files.

I will request pre-commit steps and submit.
