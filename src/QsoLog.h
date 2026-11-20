#pragma once

// QSO log: a dock with Call / RST / Name / QTH / Grid / Comment, frequency,
// mode and times filled in from the radio and the current tab; "Log QSO"
// appends an ADIF record to logs/wfview-tci-digi.adi (for QRZ, LoTW via
// TQSL, or any logger that imports ADIF).

#include <QDateTime>
#include <QDockWidget>
#include <QList>
#include <QString>
#include <functional>

class CallLookup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSettings;
class QTableWidget;

struct Qso
{
    QDateTime on, off; // UTC
    QString call, mode, submode, rstSent, rstRcvd, name, qth, grid, comment;
    qint64 freqHz = 0;
    QString myCall, myGrid;
};

namespace Adif
{
QString band(qint64 hz); // "20m", "" if outside the amateur bands
QString record(const Qso &q);
QString header();
// Parses the records of an ADIF file (the fields this app writes).
QList<Qso> parse(const QString &text);
} // namespace Adif

class QsoLogDock : public QDockWidget
{
    Q_OBJECT

public:
    QsoLogDock(QSettings *settings, QWidget *parent = nullptr);

    QString logPath() const;

public slots:
    void setRadio(qint64 hz, const QString &modulation);
    // ADIF MODE / SUBMODE of the tab in use, and how it is shown.
    void setMode(const QString &mode, const QString &submode, const QString &shown);
    void setCall(const QString &call);
    void setMyCall(const QString &call);
    void setMyGrid(const QString &grid);

public:
    // Name / QTH / Grid filled from QRZ or HamQTH when a call is entered (blank fields only).
    void setLookup(CallLookup *lookup);
    // "SNR 18 dB, S7" for the signal being received now (empty if not known); added to the
    // comment when the QSO is logged, if "Add signal report" is ticked.
    void setSignalNote(std::function<QString()> note)
    {
        m_signalNote = std::move(note);
    }

signals:
    void logged(const QString &line); // for the TCI log / status bar
    void qsoLogged(const Qso &q); // the record just written (PSK Reporter)
    void myGridChanged(const QString &grid);

private slots:
    void logQso();
    void clearForm();

private:
    void addRow(const Qso &q, bool top);
    QString defaultRst() const;
    void startLookup();

    QSettings *m_settings;
    qint64 m_hz = 0;
    QString m_mode, m_submode;
    QString m_myCall;
    QDateTime m_start;

    QLineEdit *m_call = nullptr;
    QLineEdit *m_rstS = nullptr;
    QLineEdit *m_rstR = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_qth = nullptr;
    QLineEdit *m_grid = nullptr;
    QLineEdit *m_comment = nullptr;
    QLineEdit *m_myGrid = nullptr;
    QLabel *m_info = nullptr;
    QPushButton *m_log = nullptr;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_addSignal = nullptr;
    CallLookup *m_lookup = nullptr;
    std::function<QString()> m_signalNote;
};
