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
    aboutData.setLicense(KAboutLicense::Custom);
    aboutData.setLicenseText(QStringLiteral("MIT License\n\nCopyright (c) 2024 Your Name\n\nPermission is hereby granted, free of charge, to any person obtaining a copy\nof this software and associated documentation files (the \"Software\"), to deal\nin the Software without restriction, including without limitation the rights\nto use, copy, modify, merge, publish, distribute, sublicense, and/or sell\ncopies of the Software, and to permit persons to whom the Software is\nfurnished to do so, subject to the following conditions:\n\nThe above copyright notice and this permission notice shall be included in all\ncopies or substantial portions of the Software.\n\nTHE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\nIMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\nFITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\nAUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\nLIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\nOUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\nSOFTWARE."));
    KAboutData::setApplicationData(aboutData);

    MainWindow *window = new MainWindow();
    window->show();

    return app.exec();
}
