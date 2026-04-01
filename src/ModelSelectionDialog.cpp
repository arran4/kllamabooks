#include "ModelSelectionDialog.h"

#include <QDialogButtonBox>
#include <QIcon>
#include <QSettings>
#include <QVBoxLayout>

ModelSelectionDialog::ModelSelectionDialog(const QStringList& models, const QStringList& previouslySelected,
                                           QWidget* parent)
    : QDialog(parent), m_allModels(models), m_previouslySelected(previouslySelected) {
    setWindowTitle("Select Model(s)");
    resize(400, 300);
    setupUi();
}

ModelSelectionDialog::~ModelSelectionDialog() {}

QStringList ModelSelectionDialog::selectedModels() const {
    QStringList selected;
    for (int i = 0; i < m_modelList->count(); ++i) {
        QListWidgetItem* item = m_modelList->item(i);
        if (item->checkState() == Qt::Checked) {
            selected << item->data(Qt::UserRole).toString();
        }
    }
    // If none are checked, return the currently selected (highlighted) row if any
    if (selected.isEmpty() && m_modelList->currentItem()) {
        selected << m_modelList->currentItem()->data(Qt::UserRole).toString();
    }
    return selected;
}

QString ModelSelectionDialog::selectedModel() const {
    QStringList list = selectedModels();
    return list.isEmpty() ? QString() : list.first();
}

void ModelSelectionDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_searchField = new QLineEdit(this);
    m_searchField->setPlaceholderText("Search models...");
    m_searchField->setClearButtonEnabled(true);
    mainLayout->addWidget(m_searchField);

    m_modelList = new QListWidget(this);
    mainLayout->addWidget(m_modelList);

    QSettings settings;
    QStringList favorites = settings.value("favoriteModels").toStringList();

    // Populate the list
    for (const QString& model : m_allModels) {
        if (favorites.contains(model)) {
            QListWidgetItem* item = new QListWidgetItem("⭐ " + model);
            item->setData(Qt::UserRole, model);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(m_previouslySelected.contains(model) ? Qt::Checked : Qt::Unchecked);
            m_modelList->addItem(item);
        }
    }

    for (const QString& model : m_allModels) {
        if (!favorites.contains(model)) {
            QListWidgetItem* item = new QListWidgetItem(model);
            item->setData(Qt::UserRole, model);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(m_previouslySelected.contains(model) ? Qt::Checked : Qt::Unchecked);
            m_modelList->addItem(item);
        }
    }

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    // Connect signals
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ModelSelectionDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_searchField, &QLineEdit::textChanged, this, &ModelSelectionDialog::onSearchChanged);
    connect(m_modelList, &QListWidget::itemDoubleClicked, this, &ModelSelectionDialog::onItemSelected);
    // Allow enter to select if exactly one is visible
    connect(m_searchField, &QLineEdit::returnPressed, this, [this]() {
        QListWidgetItem* selected = nullptr;
        int visibleCount = 0;
        for (int i = 0; i < m_modelList->count(); ++i) {
            QListWidgetItem* item = m_modelList->item(i);
            if (!item->isHidden()) {
                visibleCount++;
                selected = item;
            }
        }
        if (visibleCount == 1 && selected) {
            selected->setCheckState(selected->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
    });
}

void ModelSelectionDialog::onSearchChanged(const QString& text) {
    QString query = text.toLower();
    for (int i = 0; i < m_modelList->count(); ++i) {
        QListWidgetItem* item = m_modelList->item(i);
        QString itemName = item->data(Qt::UserRole).toString();
        item->setHidden(!itemName.toLower().contains(query));
    }
}

void ModelSelectionDialog::onItemSelected(QListWidgetItem* item) {
    if (item) {
        item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    }
}

void ModelSelectionDialog::onAccept() { accept(); }
