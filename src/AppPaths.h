#pragma once

// Where the app keeps what it writes: logs, the QSO log, screenshots,
// received pictures and audio recordings. One base folder (Options > Data
// folder), default Documents/wfview TCI Digi, with a sub-folder for each.

#include <QString>

class QSettings;

namespace AppPaths
{
void init(QSettings *settings);
QString base();
void setBase(const QString &dir);
QString logs(); // session logs, QSO log (ADIF)
QString screenshots();
QString sstv();
QString wefax();
QString recordings();
// "20261110-184012Z", for file names
QString stamp();
// Makes name safe for a file name (no / \ : * ? " < > |).
QString safe(const QString &name);
} // namespace AppPaths
