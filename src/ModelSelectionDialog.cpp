#include "ModelSelectionDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QIcon>
#include <QSettings>
#include <QVBoxLayout>

ModelSelectionDialog::ModelSelectionDialog(const QList<OllamaModelInfo>& modelInfos, const QStringList& fallbackModels,
                                           QWidget* parent)
    : QDialog(parent), m_modelInfos(modelInfos), m_fallbackModels(fallbackModels) {
    setWindowTitle("Select Model");
    resize(600, 400);
    setupUi();
}

ModelSelectionDialog::~ModelSelectionDialog() {}

QStringList ModelSelectionDialog::selectedModels() const { return m_selectedModels; }

void ModelSelectionDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_searchField = new QLineEdit(this);
    m_searchField->setPlaceholderText("Search models...");
    m_searchField->setClearButtonEnabled(true);
    mainLayout->addWidget(m_searchField);

    m_modelTable = new QTableWidget(0, 5, this);
    m_modelTable->setHorizontalHeaderLabels({"Name", "Size", "Family", "Parameters", "Quantization"});
    m_modelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_modelTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_modelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_modelTable->horizontalHeader()->setStretchLastSection(true);
    m_modelTable->verticalHeader()->setVisible(false);
    mainLayout->addWidget(m_modelTable);

    QSettings settings;
    QStringList favorites = settings.value("favoriteModels").toStringList();

    // Determine the list of models to display
    QList<OllamaModelInfo> itemsToDisplay;
    if (!m_modelInfos.isEmpty()) {
        itemsToDisplay = m_modelInfos;
    } else {
        for (const QString& name : m_fallbackModels) {
            OllamaModelInfo info;
            info.name = name;
            itemsToDisplay.append(info);
        }
    }

    // Populate the list (favorites first)
    int row = 0;
    for (const OllamaModelInfo& info : itemsToDisplay) {
        if (favorites.contains(info.name)) {
            m_modelTable->insertRow(row);
            QTableWidgetItem* nameItem = new QTableWidgetItem("⭐ " + info.name);
            nameItem->setData(Qt::UserRole, info.name);
            m_modelTable->setItem(row, 0, nameItem);
            m_modelTable->setItem(row, 1, new QTableWidgetItem(info.size));
            m_modelTable->setItem(row, 2, new QTableWidgetItem(info.family));
            m_modelTable->setItem(row, 3, new QTableWidgetItem(info.parameterSize));
            m_modelTable->setItem(row, 4, new QTableWidgetItem(info.quantizationLevel));
            row++;
        }
    }

    for (const OllamaModelInfo& info : itemsToDisplay) {
        if (!favorites.contains(info.name)) {
            m_modelTable->insertRow(row);
            QTableWidgetItem* nameItem = new QTableWidgetItem(info.name);
            nameItem->setData(Qt::UserRole, info.name);
            m_modelTable->setItem(row, 0, nameItem);
            m_modelTable->setItem(row, 1, new QTableWidgetItem(info.size));
            m_modelTable->setItem(row, 2, new QTableWidgetItem(info.family));
            m_modelTable->setItem(row, 3, new QTableWidgetItem(info.parameterSize));
            m_modelTable->setItem(row, 4, new QTableWidgetItem(info.quantizationLevel));
            row++;
        }
    }

    m_modelTable->resizeColumnsToContents();

    // Connect signals
    connect(m_searchField, &QLineEdit::textChanged, this, &ModelSelectionDialog::onSearchChanged);
    connect(m_modelTable, &QTableWidget::itemDoubleClicked, this, &ModelSelectionDialog::onItemSelected);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ModelSelectionDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    // Allow enter to select if exactly one is visible
    connect(m_searchField, &QLineEdit::returnPressed, this, [this]() {
        QTableWidgetItem* selected = nullptr;
        int visibleCount = 0;
        for (int i = 0; i < m_modelTable->rowCount(); ++i) {
            if (!m_modelTable->isRowHidden(i)) {
                visibleCount++;
                selected = m_modelTable->item(i, 0);
            }
        }
        if (visibleCount == 1 && selected) {
            onItemSelected(selected);
        }
    });
}

void ModelSelectionDialog::onSearchChanged(const QString& text) {
    QString query = text.toLower();
    for (int i = 0; i < m_modelTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_modelTable->item(i, 0);
        if (item) {
            QString itemName = item->data(Qt::UserRole).toString();
            m_modelTable->setRowHidden(i, !itemName.toLower().contains(query));
        }
    }
}

void ModelSelectionDialog::onItemSelected(QTableWidgetItem* item) {
    if (item) {
        int row = item->row();
        QTableWidgetItem* nameItem = m_modelTable->item(row, 0);
        if (nameItem) {
            m_selectedModels.clear();
            m_selectedModels.append(nameItem->data(Qt::UserRole).toString());
            accept();
        }
    }
}

void ModelSelectionDialog::onAccept() {
    m_selectedModels.clear();
    QSet<int> selectedRows;
    for (QTableWidgetItem* item : m_modelTable->selectedItems()) {
        selectedRows.insert(item->row());
    }

    for (int row : selectedRows) {
        QTableWidgetItem* nameItem = m_modelTable->item(row, 0);
        if (nameItem) {
            m_selectedModels.append(nameItem->data(Qt::UserRole).toString());
        }
    }

    if (m_selectedModels.isEmpty() && m_modelTable->rowCount() > 0) {
        // Fallback to first visible
        for (int i = 0; i < m_modelTable->rowCount(); ++i) {
            if (!m_modelTable->isRowHidden(i)) {
                QTableWidgetItem* nameItem = m_modelTable->item(i, 0);
                if (nameItem) {
                    m_selectedModels.append(nameItem->data(Qt::UserRole).toString());
                    break;
                }
            }
        }
    }

    accept();
}
