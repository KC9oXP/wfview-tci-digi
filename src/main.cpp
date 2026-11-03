#include "MainWindow.h"

#include <QApplication>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("KC9oXP"));
    QApplication::setApplicationName(QStringLiteral("tci-cw-rtty"));
    QApplication::setApplicationDisplayName(QStringLiteral("TCI CW/RTTY"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    QSettings settings(
        QSettings::IniFormat, QSettings::UserScope, QStringLiteral("KC9oXP"), QStringLiteral("tci-cw-rtty"));
    MainWindow w(&settings);
    w.show();
    return app.exec();
}
