#ifndef DOCUMENTTEMPLATESEDITORWIDGET_H
#define DOCUMENTTEMPLATESEDITORWIDGET_H

#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

#include "DocumentTemplatesManager.h"

class DocumentTemplatesEditorWidget : public QWidget {
    Q_OBJECT
   public:
    explicit DocumentTemplatesEditorWidget(const QString& level, QWidget* parent = nullptr);
    ~DocumentTemplatesEditorWidget() override = default;

    void setTemplates(const QList<DocumentTemplate>& editableTpls, const QList<DocumentTemplate>& inheritedTpls);
    QList<DocumentTemplate> getTemplates() const;

   private slots:
    void onAdd();
    void onEdit();
    void onRemove();
    void onSelectionChanged();

   private:
    QString m_level;  // "global"
    QTableWidget* m_table;
    QPushButton* m_addBtn;
    QPushButton* m_editBtn;
    QPushButton* m_removeBtn;
};

#endif  // DOCUMENTTEMPLATESEDITORWIDGET_H
