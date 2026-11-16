#include "ClockSet.h"
#include "MainWindow.h"

#include <QApplication>
#include <QTextStream>
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

    // Elevated helper run of "Set PC clock" (see ClockSet.h): shift the clock and exit.
    {
        const QStringList args = QApplication::arguments();
        if (args.contains(QStringLiteral("--version")))
        {
            QTextStream(stdout) << "wfview TCI Digi " << APP_VERSION << Qt::endl;
            return 0;
        }
        // Testing aid for the update check: behave as if this version were running.
        const int pv = args.indexOf(QStringLiteral("--pretend-version"));
        if (pv >= 0 && pv + 1 < args.size())
            app.setProperty("pretendVersion", args.value(pv + 1));
        const int i = args.indexOf(QStringLiteral("--adjust-clock-ms"));
        if (i >= 0)
        {
            bool ok = false;
            const qint64 ms = args.value(i + 1).toLongLong(&ok);
            return ok && qAbs(ms) <= 24LL * 3600 * 1000 ? ClockSet::runAdjust(ms) : 4;
        }
    }

    QSettings settings(
        QSettings::IniFormat, QSettings::UserScope, QStringLiteral("KC9oXP"), QStringLiteral("wfview-tci-digi"));
    MainWindow w(&settings);
    w.show();
    return app.exec();
}
