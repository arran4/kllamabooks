#ifndef QUEUEWINDOW_H
#define QUEUEWINDOW_H

#include <QCheckBox>
#include <QDialog>
#include <QPushButton>
#include <QTableWidget>

#include "PromptQueueManager.h"

class QueueWindow : public QDialog {
    Q_OBJECT
   public:
    explicit QueueWindow(PromptQueueManager* queueManager, QWidget* parent = nullptr);
    ~QueueWindow();

   private slots:
    void updateTable();
    void onPauseQueueToggled(bool checked);
    void onPauseJobClicked();
    void onResumeJobClicked();
    void onCancelJobClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();

   private:
    PromptQueueManager* m_queueManager;
    QTableWidget* m_table;
    QCheckBox* m_pauseQueueCheckBox;

    QPushButton* m_pauseJobBtn;
    QPushButton* m_resumeJobBtn;
    QPushButton* m_cancelJobBtn;
    QPushButton* m_moveUpBtn;
    QPushButton* m_moveDownBtn;

    void setupUi();
    int getSelectedJobId() const;
};

#endif  // QUEUEWINDOW_H
