#include "NotificationDelegate.h"

#include <QPainter>

NotificationDelegate::NotificationDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

void NotificationDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    // notifyType Role: UserRole + 10
    // 0: none, 1: responded_to, 2: error
    int notifyType = index.data(Qt::UserRole + 10).toInt();

    QStyleOptionViewItem opt = option;
    if (notifyType != 0) {
        opt.font.setBold(true);
    }

    QStyledItemDelegate::paint(painter, opt, index);

    if (option.state & QStyle::State_Selected || option.state & QStyle::State_MouseOver) {
        painter->save();
        QPen pen(option.palette.highlight().color(), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
        painter->restore();
    }

    if (notifyType == 0) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRect rect = option.rect;
    int dotSize = 10;
    // Position dot in the top right corner of the item
    QRect dotRect(rect.right() - dotSize - 4, rect.top() + 4, dotSize, dotSize);

    if (notifyType == 1) {                          // responded_to
        painter->setBrush(QColor(0, 255, 0, 200));  // Bright green
    } else if (notifyType == 2) {                   // error
        painter->setBrush(QColor(255, 0, 0, 200));  // Bright red
    }

    painter->setPen(QPen(Qt::white, 1));
    painter->drawEllipse(dotRect);
    painter->restore();
}
