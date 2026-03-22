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
