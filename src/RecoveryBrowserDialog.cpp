#include "RecoveryBrowserDialog.h"

#include <sqlcipher/sqlite3.h>

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

#include "db/Database.h"
#include "store/RecoveryStore.h"

// ============================================================================
// Constructor
// ============================================================================

RecoveryBrowserDialog::RecoveryBrowserDialog(BookDatabase* db, QWidget* parent) : QDialog(parent), m_db(db) {
    setWindowTitle(tr("Orphaned / Recovered Data"));
    resize(820, 520);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* infoLabel =
        new QLabel(tr("Records quarantined during schema upgrades or integrity checks are preserved below. "
                      "You can inspect, export, re-import as a document/note, or permanently delete them."),
                   this);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);

    QSplitter* splitter = new QSplitter(Qt::Vertical, this);

    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels(
        {tr("ID"), tr("Source Table"), tr("Reason"), tr("Status"), tr("Title"), tr("Created")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    splitter->addWidget(m_table);

    m_detailView = new QTextEdit(this);
    m_detailView->setReadOnly(true);
    m_detailView->setPlaceholderText(tr("Select a record to inspect its content and raw payload..."));
    splitter->addWidget(m_detailView);

    mainLayout->addWidget(splitter, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_exportBtn = new QPushButton(tr("Export All JSON..."), this);
    m_reimportBtn = new QPushButton(tr("Re-import as Document"), this);
    m_deleteBtn = new QPushButton(tr("Delete Permanently"), this);
    QPushButton* closeBtn = new QPushButton(tr("Close"), this);

    m_reimportBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    btnLayout->addWidget(m_exportBtn);
    btnLayout->addWidget(m_reimportBtn);
    btnLayout->addWidget(m_deleteBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &RecoveryBrowserDialog::onSelectionChanged);
    connect(m_exportBtn, &QPushButton::clicked, this, &RecoveryBrowserDialog::onExportJson);
    connect(m_reimportBtn, &QPushButton::clicked, this, &RecoveryBrowserDialog::onReimportArtifact);
    connect(m_deleteBtn, &QPushButton::clicked, this, &RecoveryBrowserDialog::onDeletePermanently);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    refreshRecords();
}

// ============================================================================
// Slots
// ============================================================================

void RecoveryBrowserDialog::refreshRecords() {
    m_table->setRowCount(0);
    m_detailView->clear();
    if (!m_db || !m_db->isOpen()) return;

    sqlite3* raw = reinterpret_cast<sqlite3*>(m_db->getDatabaseHandleForTesting());
    if (!raw) return;
    db::Database database(raw);
    store::RecoveryStore store(database);

    const QVector<store::QuarantinedRecord> records = store.getRecords();
    for (const auto& rec : records) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto* idItem = new QTableWidgetItem(QString::number(rec.id));
        idItem->setData(Qt::UserRole, rec.id);
        m_table->setItem(row, 0, idItem);
        m_table->setItem(row, 1, new QTableWidgetItem(rec.sourceTable));
        m_table->setItem(row, 2, new QTableWidgetItem(rec.reason));
        m_table->setItem(row, 3, new QTableWidgetItem(rec.status));
        m_table->setItem(row, 4, new QTableWidgetItem(rec.title));
        m_table->setItem(row, 5, new QTableWidgetItem(rec.createdAt));
    }
}

void RecoveryBrowserDialog::onSelectionChanged() {
    const int recId = selectedRecordId();
    const bool hasSelection = (recId > 0);
    m_reimportBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
    if (!hasSelection || !m_db || !m_db->isOpen()) {
        m_detailView->clear();
        return;
    }

    sqlite3* raw = reinterpret_cast<sqlite3*>(m_db->getDatabaseHandleForTesting());
    if (!raw) return;
    db::Database database(raw);
    store::RecoveryStore store(database);
    const auto recOpt = store.getRecord(recId);
    if (!recOpt.has_value()) return;

    const QString text = QStringLiteral(
                             "Source Table: %1\nReason: %2\nStatus: %3\nTitle: %4\n\nContent:\n%5\n\nRaw "
                             "Payload JSON:\n%6")
                             .arg(recOpt->sourceTable, recOpt->reason, recOpt->status, recOpt->title, recOpt->content,
                                  recOpt->rawPayloadJson);
    m_detailView->setPlainText(text);
}

void RecoveryBrowserDialog::onExportJson() {
    if (!m_db || !m_db->isOpen()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Recovered Records"), QStringLiteral("recovered_records.json"), tr("JSON Files (*.json)"));
    if (path.isEmpty()) return;

    sqlite3* raw = reinterpret_cast<sqlite3*>(m_db->getDatabaseHandleForTesting());
    if (!raw) return;
    db::Database database(raw);
    store::RecoveryStore store(database);
    const QByteArray data = store.exportRecordsToJson();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export Failed"), tr("Could not write to %1").arg(path));
        return;
    }
    file.write(data);
}

void RecoveryBrowserDialog::onReimportArtifact() {
    const int recId = selectedRecordId();
    if (recId <= 0 || !m_db || !m_db->isOpen()) return;

    sqlite3* raw = reinterpret_cast<sqlite3*>(m_db->getDatabaseHandleForTesting());
    if (!raw) return;
    db::Database database(raw);
    store::RecoveryStore store(database);
    QString err;
    auto artIdOpt = store.reimportAsArtifact(recId, QStringLiteral("document"), 0, &err);
    if (!artIdOpt.has_value()) {
        QMessageBox::warning(this, tr("Re-import Failed"), err);
        return;
    }
    refreshRecords();
}

void RecoveryBrowserDialog::onDeletePermanently() {
    const int recId = selectedRecordId();
    if (recId <= 0 || !m_db || !m_db->isOpen()) return;

    if (QMessageBox::question(this, tr("Confirm Permanent Deletion"),
                              tr("Permanently delete quarantined record #%1? This cannot be undone.").arg(recId)) !=
        QMessageBox::Yes) {
        return;
    }

    sqlite3* raw = reinterpret_cast<sqlite3*>(m_db->getDatabaseHandleForTesting());
    if (!raw) return;
    db::Database database(raw);
    store::RecoveryStore store(database);
    QString err;
    if (!store.deleteRecordPermanently(recId, &err)) {
        QMessageBox::warning(this, tr("Delete Failed"), err);
        return;
    }
    refreshRecords();
}

// ============================================================================
// Private Helpers
// ============================================================================

int RecoveryBrowserDialog::selectedRecordId() const {
    const auto items = m_table->selectedItems();
    if (items.isEmpty()) return 0;
    return m_table->item(items.first()->row(), 0)->data(Qt::UserRole).toInt();
}
