cat << 'INEOF' > test_qtextedit33.cpp
#include <QApplication>
#include <QTextEdit>
#include <QString>
#include <QDebug>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QTextEdit edit;

    QTextCursor cursor(edit.document());
    cursor.movePosition(QTextCursor::End);
    edit.setTextCursor(cursor);
    edit.insertHtml(QString("<div style='font-weight: bold;'>[%1]</div>").arg("User"));
    edit.insertPlainText("\nHello\n\n");

    cursor = QTextCursor(edit.document());
    cursor.movePosition(QTextCursor::End);
    edit.setTextCursor(cursor);
    edit.insertHtml(QString("<div style='font-weight: bold;'>[%1]</div>").arg("Assistant"));
    edit.insertPlainText("\nHi there\n\n");

    QTextDocument* doc = edit.document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        qDebug() << "Block text:" << b.text().trimmed();
    }
    return 0;
}
INEOF
g++ -I/usr/include/x86_64-linux-gnu/qt6/QtCore -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets -I/usr/include/x86_64-linux-gnu/qt6/QtGui -I/usr/include/x86_64-linux-gnu/qt6 -fPIC -std=c++17 test_qtextedit33.cpp -o test_qtextedit33 -lQt6Core -lQt6Widgets -lQt6Gui
export QT_QPA_PLATFORM=minimal
./test_qtextedit33
