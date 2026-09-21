#!/bin/bash
patch -p1 << 'PATCH'
--- a/tests/test_SettingsApply.cpp
+++ b/tests/test_SettingsApply.cpp
@@ -75,6 +75,7 @@
     SettingsDialog dlg(nullptr);
     QTableWidget* table = dlg.findChild<QTableWidget*>();

+    table->selectRow(0);
     table->setCurrentCell(0, 0);  // Row 0 is conn-A
     QMetaObject::invokeMethod(&dlg, "onRemoveConnection");

@@ -121,6 +122,7 @@
     SettingsDialog dlg(nullptr);
     QTableWidget* table = dlg.findChild<QTableWidget*>();

+    table->selectRow(0);
     table->setCurrentCell(0, 0);
     QMetaObject::invokeMethod(&dlg, "onRemoveConnection");

@@ -128,10 +130,12 @@

     QTimer::singleShot(0, [&]() {
         QWidget* msgBox = QApplication::activeModalWidget();
         if (msgBox) {
-            msgBox->close();
+            QMetaObject::invokeMethod(msgBox, "reject");
         }
     });
+
+    dlg.show();

     QMetaObject::invokeMethod(&dlg, "onApply");
     QVariantList newConns = settings.value("llmConnections").toList();
PATCH
