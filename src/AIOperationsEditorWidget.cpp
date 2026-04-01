#include "AIOperationsEditorWidget.h"

#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTextEdit>
#include <QUuid>
#include <QVBoxLayout>

class EditOperationDialog : public QDialog {
   public:
    EditOperationDialog(const AIOperation& op, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(op.id.isEmpty() ? tr("Add Operation") : tr("Edit Operation"));
        resize(400, 300);

        QVBoxLayout* layout = new QVBoxLayout(this);
        QFormLayout* form = new QFormLayout();

        m_idEdit = new QLineEdit(this);
        m_idEdit->setText(op.id);
        if (op.id.isEmpty()) {
            m_idEdit->setText(QUuid::createUuid().toString(QUuid::WithoutBraces));
        }
        m_idEdit->setReadOnly(true);
        form->addRow(tr("ID:"), m_idEdit);

        m_nameEdit = new QLineEdit(this);
        m_nameEdit->setText(op.name);
        form->addRow(tr("Name:"), m_nameEdit);

        m_promptEdit = new QTextEdit(this);
        m_promptEdit->setPlainText(op.prompt);
        form->addRow(tr("Prompt (use {context}):"), m_promptEdit);

        layout->addLayout(form);

        QHBoxLayout* btnLayout = new QHBoxLayout();
        QPushButton* cancelBtn = new QPushButton(tr("Cancel"), this);
        QPushButton* saveBtn = new QPushButton(tr("Save"), this);
        btnLayout->addStretch();
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(saveBtn);

        layout->addLayout(btnLayout);

        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        connect(saveBtn, &QPushButton::clicked, this, &QDialog::accept);
    }

    AIOperation getOperation() const { return {m_idEdit->text(), m_nameEdit->text(), m_promptEdit->toPlainText(), ""}; }

   private:
    QLineEdit* m_idEdit;
    QLineEdit* m_nameEdit;
    QTextEdit* m_promptEdit;
};

AIOperationsEditorWidget::AIOperationsEditorWidget(const QString& level, QWidget* parent)
    : QWidget(parent), m_level(level) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Prompt"), tr("Source")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    layout->addWidget(m_table);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_addBtn = new QPushButton(tr("Add"), this);
    m_editBtn = new QPushButton(tr("Edit"), this);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    m_editBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);

    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_editBtn);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_addBtn, &QPushButton::clicked, this, &AIOperationsEditorWidget::onAdd);
    connect(m_editBtn, &QPushButton::clicked, this, &AIOperationsEditorWidget::onEdit);
    connect(m_removeBtn, &QPushButton::clicked, this, &AIOperationsEditorWidget::onRemove);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &AIOperationsEditorWidget::onSelectionChanged);
}

void AIOperationsEditorWidget::setOperations(const QList<AIOperation>& editableOps,
                                             const QList<AIOperation>& inheritedOps) {
    m_table->setRowCount(0);

    for (const AIOperation& op : editableOps) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        QTableWidgetItem* nameItem = new QTableWidgetItem(op.name);
        nameItem->setData(Qt::UserRole, op.id);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, new QTableWidgetItem(op.prompt.split('\n').first() + "..."));
        m_table->setItem(row, 2, new QTableWidgetItem(m_level));

        // Store full prompt in hidden data
        nameItem->setData(Qt::UserRole + 1, op.prompt);
    }

    for (const AIOperation& op : inheritedOps) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        QTableWidgetItem* nameItem = new QTableWidgetItem(op.name);
        nameItem->setData(Qt::UserRole, op.id);
        nameItem->setForeground(QBrush(Qt::gray));
        m_table->setItem(row, 0, nameItem);

        QTableWidgetItem* promptItem = new QTableWidgetItem(op.prompt.split('\n').first() + "...");
        promptItem->setForeground(QBrush(Qt::gray));
        m_table->setItem(row, 1, promptItem);

        QTableWidgetItem* sourceItem = new QTableWidgetItem(op.source);
        sourceItem->setForeground(QBrush(Qt::gray));
        m_table->setItem(row, 2, sourceItem);

        nameItem->setData(Qt::UserRole + 1, op.prompt);
    }
}

QList<AIOperation> AIOperationsEditorWidget::getOperations() const {
    QList<AIOperation> ops;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (m_table->item(i, 2)->text() == m_level) {
            ops.append({m_table->item(i, 0)->data(Qt::UserRole).toString(), m_table->item(i, 0)->text(),
                        m_table->item(i, 0)->data(Qt::UserRole + 1).toString(), m_level});
        }
    }
    return ops;
}

void AIOperationsEditorWidget::onAdd() {
    EditOperationDialog dlg(AIOperation{"", "", "", m_level}, this);
    if (dlg.exec() == QDialog::Accepted) {
        AIOperation op = dlg.getOperation();
        int row = 0;  // Add to top of editable list (before inherited ones)
        m_table->insertRow(row);
        QTableWidgetItem* nameItem = new QTableWidgetItem(op.name);
        nameItem->setData(Qt::UserRole, op.id);
        nameItem->setData(Qt::UserRole + 1, op.prompt);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, new QTableWidgetItem(op.prompt.split('\n').first() + "..."));
        m_table->setItem(row, 2, new QTableWidgetItem(m_level));
    }
}

void AIOperationsEditorWidget::onEdit() {
    int row = m_table->currentRow();
    if (row < 0 || m_table->item(row, 2)->text() != m_level) return;

    AIOperation op;
    op.id = m_table->item(row, 0)->data(Qt::UserRole).toString();
    op.name = m_table->item(row, 0)->text();
    op.prompt = m_table->item(row, 0)->data(Qt::UserRole + 1).toString();
    op.source = m_level;

    EditOperationDialog dlg(op, this);
    if (dlg.exec() == QDialog::Accepted) {
        AIOperation newOp = dlg.getOperation();
        m_table->item(row, 0)->setText(newOp.name);
        m_table->item(row, 0)->setData(Qt::UserRole + 1, newOp.prompt);
        m_table->item(row, 1)->setText(newOp.prompt.split('\n').first() + "...");
    }
}

void AIOperationsEditorWidget::onRemove() {
    int row = m_table->currentRow();
    if (row >= 0 && m_table->item(row, 2)->text() == m_level) {
        m_table->removeRow(row);
    }
}

void AIOperationsEditorWidget::onSelectionChanged() {
    int row = m_table->currentRow();
    bool isEditable = (row >= 0 && m_table->item(row, 2)->text() == m_level);
    m_editBtn->setEnabled(isEditable);
    m_removeBtn->setEnabled(isEditable);
}
