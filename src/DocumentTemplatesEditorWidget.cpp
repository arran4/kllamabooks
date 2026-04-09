#include "DocumentTemplatesEditorWidget.h"

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

class EditDocumentTemplateDialog : public QDialog {
   public:
    EditDocumentTemplateDialog(const DocumentTemplate& tpl, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle(tpl.id.isEmpty() ? tr("Add Document Template") : tr("Edit Document Template"));
        resize(500, 400);

        QVBoxLayout* layout = new QVBoxLayout(this);
        QFormLayout* form = new QFormLayout();

        m_idEdit = new QLineEdit(this);
        m_idEdit->setText(tpl.id);
        if (tpl.id.isEmpty()) {
            m_idEdit->setText("global:" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        }
        m_idEdit->setReadOnly(true);
        form->addRow(tr("ID:"), m_idEdit);

        m_nameEdit = new QLineEdit(this);
        m_nameEdit->setText(tpl.name);
        form->addRow(tr("Name:"), m_nameEdit);

        m_contentEdit = new QTextEdit(this);
        m_contentEdit->setPlainText(tpl.content);
        form->addRow(tr("Content:"), m_contentEdit);

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

    DocumentTemplate getTemplate() const { return {m_idEdit->text(), m_nameEdit->text(), m_contentEdit->toPlainText(), ""}; }

   private:
    QLineEdit* m_idEdit;
    QLineEdit* m_nameEdit;
    QTextEdit* m_contentEdit;
};

DocumentTemplatesEditorWidget::DocumentTemplatesEditorWidget(const QString& level, QWidget* parent)
    : QWidget(parent), m_level(level) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Content"), tr("Source")});
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

    connect(m_addBtn, &QPushButton::clicked, this, &DocumentTemplatesEditorWidget::onAdd);
    connect(m_editBtn, &QPushButton::clicked, this, &DocumentTemplatesEditorWidget::onEdit);
    connect(m_removeBtn, &QPushButton::clicked, this, &DocumentTemplatesEditorWidget::onRemove);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &DocumentTemplatesEditorWidget::onSelectionChanged);
}

void DocumentTemplatesEditorWidget::setTemplates(const QList<DocumentTemplate>& editableTpls,
                                             const QList<DocumentTemplate>& inheritedTpls) {
    m_table->setRowCount(0);

    for (const DocumentTemplate& tpl : editableTpls) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        QTableWidgetItem* nameItem = new QTableWidgetItem(tpl.name);
        nameItem->setData(Qt::UserRole, tpl.id);
        m_table->setItem(row, 0, nameItem);

        QString preview = tpl.content.section('\n', 0, 0);
        if (tpl.content.contains('\n') || tpl.content.length() > preview.length()) {
            preview += "...";
        }
        m_table->setItem(row, 1, new QTableWidgetItem(preview));
        m_table->setItem(row, 2, new QTableWidgetItem(m_level));

        // Store full content in hidden data
        nameItem->setData(Qt::UserRole + 1, tpl.content);
    }

    for (const DocumentTemplate& tpl : inheritedTpls) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        QTableWidgetItem* nameItem = new QTableWidgetItem(tpl.name);
        nameItem->setData(Qt::UserRole, tpl.id);
        nameItem->setForeground(QBrush(Qt::gray));
        m_table->setItem(row, 0, nameItem);

        QString preview = tpl.content.section('\n', 0, 0);
        if (tpl.content.contains('\n') || tpl.content.length() > preview.length()) {
            preview += "...";
        }
        QTableWidgetItem* contentItem = new QTableWidgetItem(preview);
        contentItem->setForeground(QBrush(Qt::gray));
        m_table->setItem(row, 1, contentItem);

        QTableWidgetItem* sourceItem = new QTableWidgetItem(tpl.source);
        sourceItem->setForeground(QBrush(Qt::gray));
        m_table->setItem(row, 2, sourceItem);

        nameItem->setData(Qt::UserRole + 1, tpl.content);
    }
}

QList<DocumentTemplate> DocumentTemplatesEditorWidget::getTemplates() const {
    QList<DocumentTemplate> tpls;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (m_table->item(i, 2)->text() == m_level) {
            tpls.append({m_table->item(i, 0)->data(Qt::UserRole).toString(), m_table->item(i, 0)->text(),
                        m_table->item(i, 0)->data(Qt::UserRole + 1).toString(), m_level});
        }
    }
    return tpls;
}

void DocumentTemplatesEditorWidget::onAdd() {
    EditDocumentTemplateDialog dlg(DocumentTemplate{"", "", "", m_level}, this);
    if (dlg.exec() == QDialog::Accepted) {
        DocumentTemplate tpl = dlg.getTemplate();
        int row = 0;  // Add to top of editable list (before inherited ones)
        m_table->insertRow(row);
        QTableWidgetItem* nameItem = new QTableWidgetItem(tpl.name);
        nameItem->setData(Qt::UserRole, tpl.id);
        nameItem->setData(Qt::UserRole + 1, tpl.content);
        m_table->setItem(row, 0, nameItem);

        QString preview = tpl.content.section('\n', 0, 0);
        if (tpl.content.contains('\n') || tpl.content.length() > preview.length()) {
            preview += "...";
        }
        m_table->setItem(row, 1, new QTableWidgetItem(preview));
        m_table->setItem(row, 2, new QTableWidgetItem(m_level));
    }
}

void DocumentTemplatesEditorWidget::onEdit() {
    int row = m_table->currentRow();
    if (row < 0 || m_table->item(row, 2)->text() != m_level) return;

    DocumentTemplate tpl;
    tpl.id = m_table->item(row, 0)->data(Qt::UserRole).toString();
    tpl.name = m_table->item(row, 0)->text();
    tpl.content = m_table->item(row, 0)->data(Qt::UserRole + 1).toString();
    tpl.source = m_level;

    EditDocumentTemplateDialog dlg(tpl, this);
    if (dlg.exec() == QDialog::Accepted) {
        DocumentTemplate newTpl = dlg.getTemplate();
        m_table->item(row, 0)->setText(newTpl.name);
        m_table->item(row, 0)->setData(Qt::UserRole + 1, newTpl.content);

        QString preview = newTpl.content.section('\n', 0, 0);
        if (newTpl.content.contains('\n') || newTpl.content.length() > preview.length()) {
            preview += "...";
        }
        m_table->item(row, 1)->setText(preview);
    }
}

void DocumentTemplatesEditorWidget::onRemove() {
    int row = m_table->currentRow();
    if (row >= 0 && m_table->item(row, 2)->text() == m_level) {
        m_table->removeRow(row);
    }
}

void DocumentTemplatesEditorWidget::onSelectionChanged() {
    int row = m_table->currentRow();
    bool isEditable = (row >= 0 && m_table->item(row, 2)->text() == m_level);
    m_editBtn->setEnabled(isEditable);
    m_removeBtn->setEnabled(isEditable);
}
