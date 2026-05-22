#include <iostream>
#include <chrono>
#include <QString>
#include <QCoreApplication>
#include "src/BookDatabase.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    // Create DB
    BookDatabase db("perf_test.db");
    db.open("test");

    std::cout << "Adding 500 documents..." << std::endl;
    db.addFolder(0, "Test Folder");
    for (int i = 0; i < 500; ++i) {
        db.addDocument(1, QString("Doc %1").arg(i), "Content");
    }

    // Simulate Queue items targeting documents
    std::cout << "Benchmarking unoptimized..." << std::endl;
    auto startUnopt = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= 100; ++i) { // 100 queue items
        QString targetTitle = "Unknown";
        auto docs = db.getDocuments(-1);
        for (const auto& doc : docs) {
            if (doc.id == i) {
                targetTitle = doc.title;
                break;
            }
        }
    }
    auto endUnopt = std::chrono::high_resolution_clock::now();

    std::cout << "Benchmarking optimized..." << std::endl;
    auto startOpt = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= 100; ++i) {
        QString targetTitle = "Unknown";
        auto doc = db.getDocument(i);
        if (doc) {
            targetTitle = doc->title;
        }
    }
    auto endOpt = std::chrono::high_resolution_clock::now();

    auto unoptMs = std::chrono::duration_cast<std::chrono::milliseconds>(endUnopt - startUnopt).count();
    auto optMs = std::chrono::duration_cast<std::chrono::milliseconds>(endOpt - startOpt).count();

    std::cout << "Unoptimized: " << unoptMs << " ms" << std::endl;
    std::cout << "Optimized:   " << optMs << " ms" << std::endl;

    return 0;
}
