#include <KAboutData>
#include <KLocalizedString>
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusError>

#include "MainWindow.h"

#ifndef KGHN_APP_VERSION
#define KGHN_APP_VERSION "dev"
#endif

int main(int argc, char *argv[]) {
    QCoreApplication::setOrganizationName("arran4");
    QCoreApplication::setOrganizationDomain("arran4.com");
    QCoreApplication::setApplicationName("kllamabooks");
    QCoreApplication::setApplicationVersion(QStringLiteral(KGHN_APP_VERSION));
    QGuiApplication::setDesktopFileName("com.arran4.kllamabooks.desktop");

    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon::fromTheme("kllamabooks", QIcon(":/assets/icon.png")));

    KLocalizedString::setApplicationDomain("kllamabooks");

    KAboutData aboutData(QStringLiteral("kllamabooks"), QStringLiteral("KLlamaBooks"),
                         QStringLiteral(KGHN_APP_VERSION));
    aboutData.setDesktopFileName("com.arran4.kllamabooks.desktop");
    KAboutData::setApplicationData(aboutData);

    MainWindow *window = new MainWindow();
    window->show();

    return app.exec();
}
