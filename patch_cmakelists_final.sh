#!/bin/bash
patch -p1 << 'PATCH'
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -38,6 +38,8 @@
     src/CredentialStore.h
     src/ConnectionMigration.cpp
     src/ConnectionMigration.h
+    src/AppCredentialManager.cpp
+    src/AppCredentialManager.h
     src/SettingsDialog.cpp
     src/SettingsDialog.h
     src/DatabaseSettingsDialog.cpp
PATCH
