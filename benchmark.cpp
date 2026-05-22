#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDebug>
#include "src/BookDatabase.h"

int main(int argc, char *argv[]) {
    QCoreApplication a(argc, argv);

    BookDatabase db("test_bench.db");
    db.open();
    db.checkAndApplySchemaUpdates(); // initialize

    // Add 10000 documents
    for (int i = 0; i < 10000; i++) {
        db.addDocument(0, "Doc " + QString::number(i), "Content " + QString::number(i));
    }

    // add an item to test against
    int targetId = db.addDocument(0, "Target Doc", "Target Content");

    QElapsedTimer timer;
    timer.start();

    int num_iters = 10;

    // Simulate old behavior
    for (int i = 0; i < num_iters; i++) {
        for (const auto& d : db.getDocuments(-1)) {
            if (d.id == targetId) {
                // don't actually add, just simulate find
            }
        }
    }

    qDebug() << "Old method (fetching all):" << timer.elapsed() << "ms";

    // Simulate new behavior
    timer.start();
    for (int i = 0; i < num_iters; i++) {
        auto d = db.getDocument(targetId);
        if (d) {
            // found
        }
    }

    qDebug() << "New method (fetching by id):" << timer.elapsed() << "ms";

    return 0;
}
