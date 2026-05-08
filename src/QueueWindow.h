#ifndef QUEUEWINDOW_H
#define QUEUEWINDOW_H

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "QueueManager.h"

class QueueWindow : public QWidget {
    Q_OBJECT
   public:
    explicit QueueWindow(QWidget* parent = nullptr);

   private slots:
    void refresh();
    void onCancelItem();
    void onRetryItem();
    void onModifyItem();
    void onClearCompleted();
    void onJumpItem();
    void showContextMenu(const QPoint& pos);
    void onShowDetails();

   private:
    QListWidget* m_queueList;
    QPushButton* m_clearBtn;
};

#endif  // QUEUEWINDOW_H
