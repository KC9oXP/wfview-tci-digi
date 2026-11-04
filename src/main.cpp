#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("KC9oXP"));
    QApplication::setApplicationName(QStringLiteral("wfview-tci-digi"));
    QApplication::setApplicationDisplayName(QStringLiteral("wfview TCI Digi"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/wfview-tci-digi.png")));

    QSettings settings(
        QSettings::IniFormat, QSettings::UserScope, QStringLiteral("KC9oXP"), QStringLiteral("wfview-tci-digi"));
    MainWindow w(&settings);
    w.show();
    return app.exec();
}
