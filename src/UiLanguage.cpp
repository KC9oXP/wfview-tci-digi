#include "UiLanguage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QMap>
#include <QSettings>
#include <QStringList>
#include <QTranslator>

namespace
{
const QString kPrefix = QStringLiteral("wfview_tci_digi_");

QString besideExe()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations"));
}

// Search order for our own catalogs: a hand-placed file beats the built-in one.
QStringList appDirs()
{
    return {besideExe(), QStringLiteral(":/i18n")};
}

QString settingValue(const QSettings &settings)
{
    return settings.value(QStringLiteral("UI/Language"), QStringLiteral("system")).toString();
}

bool isEnglish(const QString &code)
{
    return code.isEmpty() || code == QLatin1String("C") || code.section(QLatin1Char('_'), 0, 0) == QLatin1String("en");
}

// "ja_JP" -> {"ja_JP", "ja"}
QStringList candidates(const QString &code)
{
    QStringList list;
    if (!code.isEmpty())
        list << code;
    const QString lang = code.section(QLatin1Char('_'), 0, 0);
    if (!lang.isEmpty() && !list.contains(lang))
        list << lang;
    return list;
}

bool loadFirst(QTranslator *tr, const QStringList &names, const QStringList &dirs)
{
    for (const QString &name : names)
        for (const QString &dir : dirs)
            if (tr->load(QDir(dir).filePath(name)))
                return true;
    return false;
}
} // namespace

namespace UiLanguage
{
QList<Entry> available()
{
    QMap<QString, Entry> found;
    for (const QString &dir : appDirs())
    {
        const QStringList files =
            QDir(dir).entryList(QStringList() << kPrefix + QStringLiteral("*.qm"), QDir::Files, QDir::Name);
        for (const QString &file : files)
        {
            const QString code = QFileInfo(file).completeBaseName().mid(kPrefix.size());
            if (code.isEmpty() || isEnglish(code) || found.contains(code))
                continue;
            found.insert(code, Entry{code, displayName(code)});
        }
    }
    return found.values();
}

QString install(QCoreApplication &app, const QSettings &settings)
{
    const QString wanted = settingValue(settings);
    const QString code = wanted == QLatin1String("system") ? QLocale::system().name() : wanted;
    if (isEnglish(code))
        return QString();

    // Parent = app: the translators live as long as the application.
    auto *ours = new QTranslator(&app);
    QStringList names;
    for (const QString &c : candidates(code))
        names << kPrefix + c + QStringLiteral(".qm");
    if (!loadFirst(ours, names, appDirs()))
    {
        delete ours;
        return QString(); // no catalog for this language: stay in English
    }
    app.installTranslator(ours);

    // Qt's own dialog buttons etc. Optional: English if not shipped.
    auto *qt = new QTranslator(&app);
    const QStringList qtDirs = {besideExe(), QLibraryInfo::path(QLibraryInfo::TranslationsPath)};
    QStringList qtNames;
    for (const QString &c : candidates(code))
        qtNames << QStringLiteral("qtbase_") + c + QStringLiteral(".qm") << QStringLiteral("qt_") + c + QStringLiteral(".qm");
    if (loadFirst(qt, qtNames, qtDirs))
        app.installTranslator(qt);
    else
        delete qt;

    return ours->language().isEmpty() ? code : ours->language();
}

QString displayName(const QString &code)
{
    if (code == QLatin1String("system"))
        return QCoreApplication::translate("MainWindow", "System language");
    if (isEnglish(code))
        return QStringLiteral("English");
    QString name = QLocale(code).nativeLanguageName();
    if (name.isEmpty())
        return code;
    return name + QStringLiteral(" [") + code + QStringLiteral("]");
}
} // namespace UiLanguage
