#include "AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace
{
QSettings *g_settings = nullptr;

QString sub(const QString &name)
{
    const QString p = QDir(AppPaths::base()).filePath(name);
    QDir().mkpath(p);
    return p;
}
} // namespace

namespace AppPaths
{
void init(QSettings *settings)
{
    g_settings = settings;
}

QString base()
{
    QString def = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (def.isEmpty())
        def = QDir::homePath();
    def = QDir(def).filePath(QStringLiteral("wfview TCI Digi"));
    const QString p = g_settings ? g_settings->value(QStringLiteral("Paths/Data"), def).toString() : def;
    QDir().mkpath(p);
    return p;
}

void setBase(const QString &dir)
{
    if (g_settings)
        g_settings->setValue(QStringLiteral("Paths/Data"), dir);
}

QString logs()
{
    return sub(QStringLiteral("logs"));
}

QString screenshots()
{
    return sub(QStringLiteral("screenshots"));
}

QString sstv()
{
    return sub(QStringLiteral("sstv"));
}

QString wefax()
{
    return sub(QStringLiteral("wefax"));
}

QString recordings()
{
    return sub(QStringLiteral("recordings"));
}

QString stamp()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss'Z'"));
}

QString safe(const QString &name)
{
    QString s = name;
    s.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]+")), QStringLiteral("_"));
    return s.trimmed();
}
} // namespace AppPaths
