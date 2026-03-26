#ifndef NOTIFICATIONDELEGATE_H
#define NOTIFICATIONDELEGATE_H

#include <QPainter>
#include <QStyledItemDelegate>

#include "BookDatabase.h"

class NotificationDelegate : public QStyledItemDelegate {
    Q_OBJECT
   public:
    explicit NotificationDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // We might need a way to check notifications for a given item
    // Since we don't have direct access to DB here easily, we might store
    // notification status in the model's user role.
};

#endif  // NOTIFICATIONDELEGATE_H
