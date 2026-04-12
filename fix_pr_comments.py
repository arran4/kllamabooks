import sys

def main():
    # Fix DocumentEditWindow.cpp
    with open('src/DocumentEditWindow.cpp', 'r') as f:
        content = f.read()

    # Save -> KStandardAction::save
    content = content.replace(
        'QAction* saveAction = new QAction(QIcon::fromTheme("document-save"), m_targetType == "draft" ? tr("Update Draft") : tr("Save"), this);\n    actionCollection()->addAction(QStringLiteral("document_save"), saveAction);\n    actionCollection()->setDefaultShortcut(saveAction, QKeySequence::Save);',
        'QAction* saveAction = KStandardAction::save(this, &DocumentEditWindow::onSaveClicked, actionCollection());\n    if (m_targetType == "draft") saveAction->setText(tr("Update Draft"));\n    actionCollection()->addAction(QStringLiteral("document_save"), saveAction);'
    )
    # The previous replace didn\'t remove the connect as KStandardAction connects it.
    # Actually the KStandardAction::save takes (const QObject *recvr, const char *slot, QActionCollection *parent)
    # We will use KStandardAction::save(this, &DocumentEditWindow::onSaveClicked, actionCollection());
    # Wait, KStandardAction::save takes a slot as a char* or member function pointer in KF6.

    # Save As -> KStandardAction::saveAs
    content = content.replace(
        'QAction* saveAsAction = new QAction(QIcon::fromTheme("document-save-as"), tr("Save As..."), this);\n    actionCollection()->addAction(QStringLiteral("document_save_as"), saveAsAction);\n    actionCollection()->setDefaultShortcut(saveAsAction, QKeySequence::SaveAs);\n    connect(saveAsAction, &QAction::triggered, this, &DocumentEditWindow::onSaveAsClicked);',
        'QAction* saveAsAction = KStandardAction::saveAs(this, &DocumentEditWindow::onSaveAsClicked, actionCollection());\n    actionCollection()->addAction(QStringLiteral("document_save_as"), saveAsAction);'
    )

    # Close -> KStandardAction::close
    content = content.replace(
        'QAction* closeAction = new QAction(QIcon::fromTheme("window-close"), tr("Close"), this);\n    actionCollection()->addAction(QStringLiteral("file_close"), closeAction);\n    actionCollection()->setDefaultShortcut(closeAction, QKeySequence::Close);\n    connect(closeAction, &QAction::triggered, this, &QWidget::close);',
        'QAction* closeAction = KStandardAction::close(this, &QWidget::close, actionCollection());\n    actionCollection()->addAction(QStringLiteral("file_close"), closeAction);'
    )

    with open('src/DocumentEditWindow.cpp', 'w') as f:
        f.write(content)

    # Fix MainWindow.cpp
    with open('src/MainWindow.cpp', 'r') as f:
        content = f.read()

    # New Book -> KStandardAction::openNew
    content = content.replace(
        'QAction* newBookAction = new QAction(QIcon::fromTheme("document-new"), tr("New Book"), this);\n    actionCollection()->addAction(QStringLiteral("new_book"), newBookAction);\n    actionCollection()->setDefaultShortcut(newBookAction, QKeySequence::New);',
        'QAction* newBookAction = KStandardAction::openNew(this, &MainWindow::onCreateBook, actionCollection());\n    newBookAction->setText(tr("New Book"));\n    actionCollection()->addAction(QStringLiteral("new_book"), newBookAction);'
    )

    # Open Book -> KStandardAction::open
    content = content.replace(
        'QAction* openBookAction = new QAction(QIcon::fromTheme("document-open"), tr("Open Book"), this);\n    actionCollection()->addAction(QStringLiteral("open_book"), openBookAction);\n    actionCollection()->setDefaultShortcut(openBookAction, QKeySequence::Open);\n    connect(openBookAction, &QAction::triggered, this, &MainWindow::onOpenBook);',
        'QAction* openBookAction = KStandardAction::open(this, &MainWindow::onOpenBook, actionCollection());\n    openBookAction->setText(tr("Open Book"));\n    actionCollection()->addAction(QStringLiteral("open_book"), openBookAction);'
    )

    # Close Book -> let's keep as is or KStandardAction::close? The comment says "In a KXmlGuiWindow, standard actions like 'New' should be created using KStandardAction" for new_book, and openBookAction. Wait, the comment says:
    # "Use KStandardAction::open for the 'Open Book' action"
    # "In a KXmlGuiWindow, standard actions like 'New' should be created using KStandardAction"

    # We need to remove the previous manual connects for those actions too.
    content = content.replace('connect(newBookAction, &QAction::triggered, this, &MainWindow::onCreateBook);', '// connect removed, KStandardAction handles it')

    with open('src/MainWindow.cpp', 'w') as f:
        f.write(content)

if __name__ == "__main__":
    main()
