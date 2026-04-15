1. **Update `MergeDocumentsDialog.h`**: Add `m_actionTypeCombo`, `m_documentTitles`, `m_isRegenerating` and `MergeAction` enum (ReplaceExisting, NewDocument, ReplaceDocA, ReplaceDocB, ReplaceDocAandB).
2. **Update `MergeDocumentsDialog.cpp`**:
   - Add the action layout and `m_actionTypeCombo` to the dialog.
   - Implement `setIsRegenerating()` to populate the combo box dynamically depending on `isRegenerating` and number of sources.
   - Extract titles from the fetched documents.
   - Implement getter to fetch selected action.
3. **Update `MainWindow.cpp`'s `onRegenerateMerge`**: Call `dlg.setIsRegenerating(true)` before `exec()`. Handle the returned action to determine `existingDocId` and delete logic for source documents.
4. **Update `MainWindow.cpp`'s `onMergeDocumentsSelected`**: Handle the new `MergeAction` values (ReplaceSingle, ReplaceAll, NewDocument) and pass the corresponding `existingDocId` and delete logic.
5. **Update `MainWindow.cpp`'s `processMergeGeneration`**: Ensure it handles creating new merges or replacing appropriately.
6. **Pre commit instructions**: Call `pre_commit_instructions` tool to run checks.
