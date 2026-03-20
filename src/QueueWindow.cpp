#include "QueueWindow.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QVBoxLayout>

QueueWindow::QueueWindow(PromptQueueManager* queueManager, QWidget* parent)
    : QDialog(parent), m_queueManager(queueManager) {
    setWindowTitle("Prompt Queue");
    resize(800, 400);
    setupUi();

    connect(m_queueManager, &PromptQueueManager::queueUpdated, this, &QueueWindow::updateTable);
    connect(m_queueManager, &PromptQueueManager::jobStatusChanged, this, &QueueWindow::updateTable);

    updateTable();
}

QueueWindow::~QueueWindow() {}

void QueueWindow::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_pauseQueueCheckBox = new QCheckBox("Pause Entire Queue", this);
    m_pauseQueueCheckBox->setChecked(m_queueManager->isQueuePaused());
    connect(m_pauseQueueCheckBox, &QCheckBox::toggled, this, &QueueWindow::onPauseQueueToggled);
    mainLayout->addWidget(m_pauseQueueCheckBox);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({"ID", "Book", "Prompt Preview", "Status", "Error"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_table);

    QHBoxLayout* btnLayout = new QHBoxLayout();

    m_pauseJobBtn = new QPushButton("Pause Job", this);
    m_resumeJobBtn = new QPushButton("Resume Job", this);
    m_cancelJobBtn = new QPushButton("Cancel Job", this);
    m_moveUpBtn = new QPushButton("Move Up", this);
    m_moveDownBtn = new QPushButton("Move Down", this);

    btnLayout->addWidget(m_pauseJobBtn);
    btnLayout->addWidget(m_resumeJobBtn);
    btnLayout->addWidget(m_cancelJobBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_moveUpBtn);
    btnLayout->addWidget(m_moveDownBtn);

    mainLayout->addLayout(btnLayout);

    connect(m_pauseJobBtn, &QPushButton::clicked, this, &QueueWindow::onPauseJobClicked);
    connect(m_resumeJobBtn, &QPushButton::clicked, this, &QueueWindow::onResumeJobClicked);
    connect(m_cancelJobBtn, &QPushButton::clicked, this, &QueueWindow::onCancelJobClicked);
    connect(m_moveUpBtn, &QPushButton::clicked, this, &QueueWindow::onMoveUpClicked);
    connect(m_moveDownBtn, &QPushButton::clicked, this, &QueueWindow::onMoveDownClicked);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        bool hasSelection = m_table->currentRow() >= 0;
        m_pauseJobBtn->setEnabled(hasSelection);
        m_resumeJobBtn->setEnabled(hasSelection);
        m_cancelJobBtn->setEnabled(hasSelection);
        m_moveUpBtn->setEnabled(hasSelection && m_table->currentRow() > 0);
        m_moveDownBtn->setEnabled(hasSelection && m_table->currentRow() < m_table->rowCount() - 1);
    });
}

void QueueWindow::updateTable() {
    int currentRow = m_table->currentRow();
    int currentId = getSelectedJobId();

    m_table->setRowCount(0);

    QList<QueueJob> jobs = m_queueManager->getJobs();
    for (const QueueJob& job : jobs) {
        int row = m_table->rowCount();
        m_table->insertRow(row);

        QTableWidgetItem* idItem = new QTableWidgetItem(QString::number(job.id));
        idItem->setData(Qt::UserRole, job.id);

        QFileInfo fi(job.bookFilepath);
        QTableWidgetItem* bookItem = new QTableWidgetItem(fi.fileName());

        QString preview = job.promptText.simplified();
        if (preview.length() > 50) preview = preview.left(47) + "...";
        QTableWidgetItem* promptItem = new QTableWidgetItem(preview);

        QTableWidgetItem* statusItem = new QTableWidgetItem(job.status);
        QTableWidgetItem* errorItem = new QTableWidgetItem(job.errorText);

        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        bookItem->setFlags(bookItem->flags() & ~Qt::ItemIsEditable);
        promptItem->setFlags(promptItem->flags() & ~Qt::ItemIsEditable);
        statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
        errorItem->setFlags(errorItem->flags() & ~Qt::ItemIsEditable);

        m_table->setItem(row, 0, idItem);
        m_table->setItem(row, 1, bookItem);
        m_table->setItem(row, 2, promptItem);
        m_table->setItem(row, 3, statusItem);
        m_table->setItem(row, 4, errorItem);

        if (job.id == currentId) {
            m_table->selectRow(row);
        }
    }
}

int QueueWindow::getSelectedJobId() const {
    int row = m_table->currentRow();
    if (row >= 0 && m_table->item(row, 0)) {
        return m_table->item(row, 0)->data(Qt::UserRole).toInt();
    }
    return -1;
}

void QueueWindow::onPauseQueueToggled(bool checked) {
    if (checked)
        m_queueManager->pauseQueue();
    else
        m_queueManager->resumeQueue();
}

void QueueWindow::onPauseJobClicked() {
    int id = getSelectedJobId();
    if (id != -1) m_queueManager->pauseJob(id);
}

void QueueWindow::onResumeJobClicked() {
    int id = getSelectedJobId();
    if (id != -1) m_queueManager->resumeJob(id);
}

void QueueWindow::onCancelJobClicked() {
    int id = getSelectedJobId();
    if (id != -1) {
        if (QMessageBox::question(this, "Cancel Job", "Are you sure you want to cancel this job?") ==
            QMessageBox::Yes) {
            m_queueManager->cancelJob(id);
        }
    }
}

void QueueWindow::onMoveUpClicked() {
    int id = getSelectedJobId();
    if (id != -1) m_queueManager->moveJobUp(id);
}

void QueueWindow::onMoveDownClicked() {
    int id = getSelectedJobId();
    if (id != -1) m_queueManager->moveJobDown(id);  // Wait, fix typo
}
