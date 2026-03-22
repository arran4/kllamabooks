#include <QApplication>
#include <QListView>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>

class MyListView : public QListView {
public:
    MyListView(QWidget* parent = nullptr) : QListView(parent) {
        setAcceptDrops(true);
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDragDropOverwriteMode(true);
        setDropIndicatorShown(true);
        setViewMode(QListView::IconMode);
    }

protected:
    void dragMoveEvent(QDragMoveEvent *e) override {
        QListView::dragMoveEvent(e);
        qDebug() << "DragMoveEvent in List View, Pos:" << e->position().toPoint() << "Accepted:" << e->isAccepted();

        QModelIndex targetIndex = indexAt(e->position().toPoint());
        if (targetIndex.isValid()) {
            QStandardItemModel* mdl = static_cast<QStandardItemModel*>(model());
            QStandardItem* targetItem = mdl->itemFromIndex(targetIndex);
            qDebug() << "Target item:" << targetItem->text() << "DropEnabled:" << (targetItem->flags() & Qt::ItemIsDropEnabled);
            if (targetItem->flags() & Qt::ItemIsDropEnabled) {
                e->acceptProposedAction();
            }
        } else {
            e->acceptProposedAction(); // background drop
        }
    }
};

class TestWidget : public QWidget {
public:
    TestWidget() {
        QVBoxLayout* layout = new QVBoxLayout(this);

        MyListView* view1 = new MyListView();
        QStandardItemModel* model1 = new QStandardItemModel();
        view1->setModel(model1);

        QStandardItem* item1 = new QStandardItem("Item 1");
        item1->setFlags(item1->flags() & ~Qt::ItemIsDropEnabled);

        QStandardItem* item2 = new QStandardItem("Folder 1");
        item2->setFlags(item2->flags() | Qt::ItemIsDropEnabled);

        model1->appendRow(item1);
        model1->appendRow(item2);

        layout->addWidget(view1);
        resize(300, 300);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    TestWidget w;
    w.show();
    return app.exec();
}
