#ifndef AIOPERATIONSEDITORWIDGET_H
#define AIOPERATIONSEDITORWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include "AIOperationsManager.h"

class AIOperationsEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit AIOperationsEditorWidget(const QString& level, QWidget* parent = nullptr);
    ~AIOperationsEditorWidget() override = default;

    void setOperations(const QList<AIOperation>& editableOps, const QList<AIOperation>& inheritedOps);
    QList<AIOperation> getOperations() const;

private slots:
    void onAdd();
    void onEdit();
    void onRemove();
    void onSelectionChanged();

private:
    QString m_level; // "global" or "database"
    QTableWidget* m_table;
    QPushButton* m_addBtn;
    QPushButton* m_editBtn;
    QPushButton* m_removeBtn;
};

#endif // AIOPERATIONSEDITORWIDGET_H
