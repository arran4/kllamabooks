#include <QtTest>
#include <QFile>
#include <QDir>
#include "../src/BookDatabase.h"

class TestBookDatabase : public QObject {
    Q_OBJECT
private slots:
    void testOpenInvalidPath() {
        BookDatabase db("/nonexistent_directory_for_test/test.db");
        QVERIFY(db.open("password") == false);
    }

    void testOpenInvalidPassword() {
        QString dbPath = QDir::temp().filePath("test_db_invalid_pwd.db");
        QFile::remove(dbPath);

        {
            BookDatabase db(dbPath);
            QVERIFY(db.open("correct_password"));
        }

        {
            BookDatabase db(dbPath);
            QVERIFY(db.open("wrong_password") == false);
        }

        QFile::remove(dbPath);
    }

    void testOpenCorruptedDatabase() {
        QString dbPath = QDir::temp().filePath("test_db_corrupted.db");
        QFile::remove(dbPath);

        QFile file(dbPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("This is a garbage text file that is not a valid SQLite database.");
        file.close();

        BookDatabase db(dbPath);
        QVERIFY(db.open("any_password") == false);
        QVERIFY(db.open("") == false);

        QFile::remove(dbPath);
    }
};

QTEST_MAIN(TestBookDatabase)
#include "test_BookDatabase.moc"
