# Plan

1. Remove any pending writes from `SettingsDialog::saveConnections()`. We will strictly **never** write new plaintext credentials to `QSettings`. If KWallet fails, the newly added/edited credential must simply fail to save and not be written to `QSettings`.
2. Redesign `SettingsDialog::onApply()` for atomic/safe staging:
    - Attempt KWallet `writeCredential` and `deleteCredential` first.
    - If a specific write fails, revert its corresponding UI change in the table / variant map and warn the user.
    - Do NOT write new `authKey`s to `QSettings`. Only write back the connections list (`hasCredential`, `id`, `name`, etc.). Unmigrated legacy credentials should just be kept as is if not modified.
3. Migrate `loadConnections()` properly:
    - If migration fails, don't throw away the legacy credential. Keep it in `QSettings` until KWallet finally works.
4. Clean up the unused `fallbackAuthKey` in `onTestConnection()`.
5. Update tests to verify that new plaintext isn't written to QSettings on write failures.
6. Run clang-format and make sure the tests pass.
