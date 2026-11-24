#pragma once

// Interface language (Options > Language).
//
// Catalogs are Qt Linguist .qm files named wfview_tci_digi_<locale>.qm. They are
// looked for in two places:
//   - ":/i18n"                  built into the executable (CONFIG += embed_translations)
//   - "<exe folder>/translations" a catalog dropped in by hand (new or updated
//                                language without rebuilding); it wins over the
//                                built-in one of the same name.
// Qt's own strings (Yes/No/Cancel ...) come from qtbase_<lang>.qm / qt_<lang>.qm,
// searched in "<exe folder>/translations" and Qt's translations path.
//
// The setting UI/Language is "system" (follow the OS locale, default), "en"
// (English source strings) or a locale such as "ja_JP". It takes effect at the
// next start.

#include <QList>
#include <QString>

class QCoreApplication;
class QSettings;

namespace UiLanguage
{
struct Entry
{
    QString code;  // "ja_JP"
    QString label; // "日本語 [ja_JP]"
};

// Languages with a catalog (built-in or beside the executable), sorted by code,
// without English (the source language).
QList<Entry> available();

// Load the catalog for the language in settings (UI/Language) plus Qt's own
// catalog for it. Returns the locale actually loaded, or an empty string when
// the interface stays in English.
QString install(QCoreApplication &app, const QSettings &settings);

// Human-readable name for a setting value ("system", "en", "ja_JP").
QString displayName(const QString &code);
} // namespace UiLanguage
