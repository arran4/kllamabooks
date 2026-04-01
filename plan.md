1. Add `AIOperationsManager` class to handle fetching, saving, and merging AI operations from built-in, global, and database sources.
2. Update `CMakeLists.txt` to include `src/AIOperationsManager.cpp` and `src/AIOperationsManager.h`.
3. Modify `SettingsDialog` to include a new tab or section for modifying global AI operations.
4. Modify `DatabaseSettingsDialog` to include a new tab or section for modifying database AI operations.
5. Create a unified `AIOperationsEditorWidget` or similar UI component to avoid code duplication in both settings dialogs.
6. Modify `AIOperationsDialog` to accept the `BookDatabase*` object, fetch the unified list of operations using `AIOperationsManager::getMergedOperations`, and populate the dropdown dynamically.
7. Update `MainWindow` to pass the `currentDb` to `AIOperationsDialog` when instantiated.
8. Complete pre-commit steps to ensure proper testing, verification, review, and reflection are done.
9. Compile and test the application to ensure the changes are applied successfully.
10. Submit.
