#ifndef QUEUEWINDOW_H
#define QUEUEWINDOW_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include "QueueManager.h"

class QueueWindow : public QDialog {
    Q_OBJECT
public:
    explicit QueueWindow(QWidget* parent = nullptr);

private slots:
    void refresh();
    void onCancelItem();
    void onMoveUp();
    void onMoveDown();
    void onClearCompleted();

private:
    QListWidget* m_queueList;
    QPushButton* m_cancelBtn;
    QPushButton* m_upBtn;
    QPushButton* m_downBtn;
    QPushButton* m_clearBtn;

    void updateButtons();
};

#endif // QUEUEWINDOW_H
