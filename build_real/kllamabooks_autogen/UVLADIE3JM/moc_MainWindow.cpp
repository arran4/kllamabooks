/****************************************************************************
** Meta object code from reading C++ file 'MainWindow.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.8.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/MainWindow.h"
#include <QtGui/qtextcursor.h>
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MainWindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.8.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN10MainWindowE_t {};
} // unnamed namespace


#ifdef QT_MOC_HAS_STRINGDATA
static constexpr auto qt_meta_stringdata_ZN10MainWindowE = QtMocHelpers::stringData(
    "MainWindow",
    "onCreateBook",
    "",
    "onOpenBook",
    "onCloseBook",
    "onOpenBookLocation",
    "showModelExplorer",
    "showSettingsDialog",
    "onBookSelected",
    "QModelIndex",
    "index",
    "onPullProgressUpdated",
    "modelName",
    "percent",
    "status",
    "onPullFinished",
    "onSendMessage",
    "onOllamaChunk",
    "chunk",
    "onOllamaComplete",
    "fullResponse",
    "onOllamaError",
    "error",
    "onItemChanged",
    "QStandardItem*",
    "item",
    "onChatNodeSelected",
    "current",
    "previous",
    "onActiveEndpointChanged",
    "onConnectionStatusChanged",
    "isOk",
    "updateEndpointsList",
    "updateBreadcrumbs",
    "refreshVfsExplorer",
    "onBreadcrumbClicked",
    "type",
    "id",
    "onRenameCurrentItem",
    "onDiscardChanges",
    "showBookContextMenu",
    "pos",
    "showOpenBookContextMenu",
    "showVfsContextMenu",
    "showInputSettingsMenu",
    "updateInputBehavior",
    "exportChatSession",
    "importChatSession",
    "onOpenBooksSelectionChanged",
    "QItemSelection",
    "selected",
    "deselected",
    "updateQueueStatus",
    "updateNotificationStatus",
    "showNotificationMenu",
    "showQueueWindow",
    "onQueueItemClicked",
    "std::shared_ptr<BookDatabase>",
    "db",
    "messageId",
    "updateTreeMarkersRecursive",
    "parent",
    "QList<Notification>",
    "notifications",
    "updateVfsMarkers",
    "moveItemToFolder",
    "draggedItem",
    "targetItem",
    "isCopy",
    "onQueueChunk",
    "onProcessingStarted",
    "onProcessingFinished",
    "success"
);
#else  // !QT_MOC_HAS_STRINGDATA
#error "qtmochelpers.h not found or too old."
#endif // !QT_MOC_HAS_STRINGDATA

Q_CONSTINIT static const uint qt_meta_data_ZN10MainWindowE[] = {

 // content:
      12,       // revision
       0,       // classname
       0,    0, // classinfo
      43,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  272,    2, 0x08,    1 /* Private */,
       3,    0,  273,    2, 0x08,    2 /* Private */,
       4,    0,  274,    2, 0x08,    3 /* Private */,
       5,    0,  275,    2, 0x08,    4 /* Private */,
       6,    0,  276,    2, 0x08,    5 /* Private */,
       7,    0,  277,    2, 0x08,    6 /* Private */,
       8,    1,  278,    2, 0x08,    7 /* Private */,
      11,    3,  281,    2, 0x08,    9 /* Private */,
      15,    1,  288,    2, 0x08,   13 /* Private */,
      16,    0,  291,    2, 0x08,   15 /* Private */,
      17,    1,  292,    2, 0x08,   16 /* Private */,
      19,    1,  295,    2, 0x08,   18 /* Private */,
      21,    1,  298,    2, 0x08,   20 /* Private */,
      23,    1,  301,    2, 0x08,   22 /* Private */,
      26,    2,  304,    2, 0x08,   24 /* Private */,
      29,    1,  309,    2, 0x08,   27 /* Private */,
      30,    1,  312,    2, 0x08,   29 /* Private */,
      32,    0,  315,    2, 0x08,   31 /* Private */,
      33,    0,  316,    2, 0x08,   32 /* Private */,
      34,    0,  317,    2, 0x08,   33 /* Private */,
      35,    2,  318,    2, 0x08,   34 /* Private */,
      38,    0,  323,    2, 0x08,   37 /* Private */,
      39,    0,  324,    2, 0x08,   38 /* Private */,
      40,    1,  325,    2, 0x08,   39 /* Private */,
      42,    1,  328,    2, 0x08,   41 /* Private */,
      43,    1,  331,    2, 0x08,   43 /* Private */,
      44,    0,  334,    2, 0x08,   45 /* Private */,
      45,    0,  335,    2, 0x08,   46 /* Private */,
      46,    0,  336,    2, 0x08,   47 /* Private */,
      47,    0,  337,    2, 0x08,   48 /* Private */,
      48,    2,  338,    2, 0x08,   49 /* Private */,
      52,    0,  343,    2, 0x08,   52 /* Private */,
      53,    0,  344,    2, 0x08,   53 /* Private */,
      54,    0,  345,    2, 0x08,   54 /* Private */,
      55,    0,  346,    2, 0x08,   55 /* Private */,
      56,    2,  347,    2, 0x08,   56 /* Private */,
      60,    2,  352,    2, 0x08,   59 /* Private */,
      64,    1,  357,    2, 0x08,   62 /* Private */,
      65,    3,  360,    2, 0x08,   64 /* Private */,
      65,    2,  367,    2, 0x28,   68 /* Private | MethodCloned */,
      69,    3,  372,    2, 0x08,   71 /* Private */,
      70,    2,  379,    2, 0x08,   75 /* Private */,
      71,    3,  384,    2, 0x08,   78 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 9,   10,
    QMetaType::Void, QMetaType::QString, QMetaType::Int, QMetaType::QString,   12,   13,   14,
    QMetaType::Void, QMetaType::QString,   12,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   18,
    QMetaType::Void, QMetaType::QString,   20,
    QMetaType::Void, QMetaType::QString,   22,
    QMetaType::Void, 0x80000000 | 24,   25,
    QMetaType::Void, 0x80000000 | 9, 0x80000000 | 9,   27,   28,
    QMetaType::Void, QMetaType::Int,   10,
    QMetaType::Void, QMetaType::Bool,   31,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::Int,   36,   37,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QPoint,   41,
    QMetaType::Void, QMetaType::QPoint,   41,
    QMetaType::Void, QMetaType::QPoint,   41,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 49, 0x80000000 | 49,   50,   51,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 57, QMetaType::Int,   58,   59,
    QMetaType::Void, 0x80000000 | 24, 0x80000000 | 62,   61,   63,
    QMetaType::Void, 0x80000000 | 62,   63,
    QMetaType::Bool, 0x80000000 | 24, 0x80000000 | 24, QMetaType::Bool,   66,   67,   68,
    QMetaType::Bool, 0x80000000 | 24, 0x80000000 | 24,   66,   67,
    QMetaType::Void, 0x80000000 | 57, QMetaType::Int, QMetaType::QString,   58,   59,   18,
    QMetaType::Void, 0x80000000 | 57, QMetaType::Int,   58,   59,
    QMetaType::Void, 0x80000000 | 57, QMetaType::Int, QMetaType::Bool,   58,   59,   72,

       0        // eod
};

Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<KXmlGuiWindow::staticMetaObject>(),
    qt_meta_stringdata_ZN10MainWindowE.offsetsAndSizes,
    qt_meta_data_ZN10MainWindowE,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_tag_ZN10MainWindowE_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<MainWindow, std::true_type>,
        // method 'onCreateBook'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onOpenBook'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onCloseBook'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onOpenBookLocation'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'showModelExplorer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'showSettingsDialog'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onBookSelected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QModelIndex &, std::false_type>,
        // method 'onPullProgressUpdated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onPullFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onSendMessage'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onOllamaChunk'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onOllamaComplete'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onOllamaError'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onItemChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QStandardItem *, std::false_type>,
        // method 'onChatNodeSelected'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QModelIndex &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QModelIndex &, std::false_type>,
        // method 'onActiveEndpointChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onConnectionStatusChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'updateEndpointsList'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateBreadcrumbs'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'refreshVfsExplorer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onBreadcrumbClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onRenameCurrentItem'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onDiscardChanges'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'showBookContextMenu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QPoint &, std::false_type>,
        // method 'showOpenBookContextMenu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QPoint &, std::false_type>,
        // method 'showVfsContextMenu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QPoint &, std::false_type>,
        // method 'showInputSettingsMenu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateInputBehavior'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'exportChatSession'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'importChatSession'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onOpenBooksSelectionChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QItemSelection &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QItemSelection &, std::false_type>,
        // method 'updateQueueStatus'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateNotificationStatus'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'showNotificationMenu'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'showQueueWindow'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'onQueueItemClicked'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<BookDatabase>, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'updateTreeMarkersRecursive'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QStandardItem *, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QList<Notification> &, std::false_type>,
        // method 'updateVfsMarkers'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QList<Notification> &, std::false_type>,
        // method 'moveItemToFolder'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QStandardItem *, std::false_type>,
        QtPrivate::TypeAndForceComplete<QStandardItem *, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'moveItemToFolder'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QStandardItem *, std::false_type>,
        QtPrivate::TypeAndForceComplete<QStandardItem *, std::false_type>,
        // method 'onQueueChunk'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<BookDatabase>, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'onProcessingStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<BookDatabase>, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'onProcessingFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<std::shared_ptr<BookDatabase>, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>
    >,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onCreateBook(); break;
        case 1: _t->onOpenBook(); break;
        case 2: _t->onCloseBook(); break;
        case 3: _t->onOpenBookLocation(); break;
        case 4: _t->showModelExplorer(); break;
        case 5: _t->showSettingsDialog(); break;
        case 6: _t->onBookSelected((*reinterpret_cast< std::add_pointer_t<QModelIndex>>(_a[1]))); break;
        case 7: _t->onPullProgressUpdated((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        case 8: _t->onPullFinished((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 9: _t->onSendMessage(); break;
        case 10: _t->onOllamaChunk((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 11: _t->onOllamaComplete((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 12: _t->onOllamaError((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->onItemChanged((*reinterpret_cast< std::add_pointer_t<QStandardItem*>>(_a[1]))); break;
        case 14: _t->onChatNodeSelected((*reinterpret_cast< std::add_pointer_t<QModelIndex>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QModelIndex>>(_a[2]))); break;
        case 15: _t->onActiveEndpointChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 16: _t->onConnectionStatusChanged((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 17: _t->updateEndpointsList(); break;
        case 18: _t->updateBreadcrumbs(); break;
        case 19: _t->refreshVfsExplorer(); break;
        case 20: _t->onBreadcrumbClicked((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 21: _t->onRenameCurrentItem(); break;
        case 22: _t->onDiscardChanges(); break;
        case 23: _t->showBookContextMenu((*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 24: _t->showOpenBookContextMenu((*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 25: _t->showVfsContextMenu((*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 26: _t->showInputSettingsMenu(); break;
        case 27: _t->updateInputBehavior(); break;
        case 28: _t->exportChatSession(); break;
        case 29: _t->importChatSession(); break;
        case 30: _t->onOpenBooksSelectionChanged((*reinterpret_cast< std::add_pointer_t<QItemSelection>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QItemSelection>>(_a[2]))); break;
        case 31: _t->updateQueueStatus(); break;
        case 32: _t->updateNotificationStatus(); break;
        case 33: _t->showNotificationMenu(); break;
        case 34: _t->showQueueWindow(); break;
        case 35: _t->onQueueItemClicked((*reinterpret_cast< std::add_pointer_t<std::shared_ptr<BookDatabase>>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 36: _t->updateTreeMarkersRecursive((*reinterpret_cast< std::add_pointer_t<QStandardItem*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QList<Notification>>>(_a[2]))); break;
        case 37: _t->updateVfsMarkers((*reinterpret_cast< std::add_pointer_t<QList<Notification>>>(_a[1]))); break;
        case 38: { bool _r = _t->moveItemToFolder((*reinterpret_cast< std::add_pointer_t<QStandardItem*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QStandardItem*>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 39: { bool _r = _t->moveItemToFolder((*reinterpret_cast< std::add_pointer_t<QStandardItem*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QStandardItem*>>(_a[2])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 40: _t->onQueueChunk((*reinterpret_cast< std::add_pointer_t<std::shared_ptr<BookDatabase>>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        case 41: _t->onProcessingStarted((*reinterpret_cast< std::add_pointer_t<std::shared_ptr<BookDatabase>>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 42: _t->onProcessingFinished((*reinterpret_cast< std::add_pointer_t<std::shared_ptr<BookDatabase>>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 30:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 1:
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QItemSelection >(); break;
            }
            break;
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ZN10MainWindowE.stringdata0))
        return static_cast<void*>(this);
    return KXmlGuiWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = KXmlGuiWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 43)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 43;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 43)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 43;
    }
    return _id;
}
QT_WARNING_POP
