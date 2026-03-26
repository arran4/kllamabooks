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
    void onMoveUp();
    void onMoveDown();
    void onClearCompleted();

   private:
    QListWidget* m_queueList;
    QPushButton* m_cancelBtn;
    QPushButton* m_retryBtn;
    QPushButton* m_modifyBtn;
    QPushButton* m_upBtn;
    QPushButton* m_downBtn;
    QPushButton* m_clearBtn;

    void updateButtons();
};

#endif  // QUEUEWINDOW_H
