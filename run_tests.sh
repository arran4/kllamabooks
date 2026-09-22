#!/bin/bash
rm -rf build
cmake -S . -B build -G Ninja
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
