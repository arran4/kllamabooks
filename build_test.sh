#!/bin/bash
/usr/lib/qt6/libexec/moc tests/test_QueueManager.cpp -o test_QueueManager.moc
/usr/lib/qt6/libexec/moc src/QueueManager.h -o QueueManager.moc
/usr/lib/qt6/libexec/moc src/OllamaClient.h -o OllamaClient.moc
g++ -std=c++17 -fPIC -I. -I/usr/include/x86_64-linux-gnu/qt6/QtTest -I/usr/include/x86_64-linux-gnu/qt6 -I/usr/include/x86_64-linux-gnu/qt6/QtCore -I/usr/include/x86_64-linux-gnu/qt6/QtNetwork -lQt6Test -lQt6Core -lQt6Network -lsqlcipher tests/test_QueueManager.cpp src/QueueManager.cpp src/BookDatabase.cpp src/OllamaClient.cpp -o test_QM
./test_QM
