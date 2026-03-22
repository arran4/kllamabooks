cat << 'INEOF' > test_event_filter.cpp
#include <QApplication>
#include <QTextEdit>
#include <QEvent>
#include <QMouseEvent>
#include <QDebug>

class MyWidget : public QObject {
public:
    QTextEdit* chatTextArea;
    MyWidget() { chatTextArea = nullptr; }
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (chatTextArea && obj == chatTextArea->viewport()) {
            qDebug() << "Handling event";
            return true;
        }
        return false;
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MyWidget w;
    QEvent event(QEvent::None);
    qDebug() << "Testing null chatTextArea...";
    w.eventFilter(nullptr, &event);
    qDebug() << "Test complete.";
    return 0;
}
INEOF
g++ -I/usr/include/x86_64-linux-gnu/qt6/QtCore -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets -I/usr/include/x86_64-linux-gnu/qt6/QtGui -I/usr/include/x86_64-linux-gnu/qt6 -fPIC -std=c++17 test_event_filter.cpp -o test_event_filter -lQt6Core -lQt6Widgets -lQt6Gui
export QT_QPA_PLATFORM=minimal
./test_event_filter
