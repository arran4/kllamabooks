cat << 'INEOF' > test_qtextedit31.cpp
#include <QApplication>
#include <QTextEdit>
#include <QString>
#include <QDebug>
#include <QTextDocument>
#include <QTextBlock>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QTextEdit edit;
    edit.insertHtml(QString("<div style='font-weight: bold;'>[%1]</div>").arg("User"));
    edit.insertPlainText("Hello\n\n");
    edit.insertHtml(QString("<div style='font-weight: bold;'>[%1]</div>").arg("Assistant"));
    edit.insertPlainText("Hi there\n\n");

    QTextDocument* doc = edit.document();
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        qDebug() << "Block text:" << b.text().trimmed();
    }
    return 0;
}
INEOF
g++ -I/usr/include/x86_64-linux-gnu/qt6/QtCore -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets -I/usr/include/x86_64-linux-gnu/qt6/QtGui -I/usr/include/x86_64-linux-gnu/qt6 -fPIC -std=c++17 test_qtextedit31.cpp -o test_qtextedit31 -lQt6Core -lQt6Widgets -lQt6Gui
export QT_QPA_PLATFORM=minimal
./test_qtextedit31
