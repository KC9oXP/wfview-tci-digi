#pragma once

// PSK Reporter: sends the callsigns this station hears to pskreporter.info (see
// engine/pskreporter.h for the datagram format and the sending rules).
//
// Off until the user turns it on. Two ways a call gets reported:
//  - Logged: every QSO logged in the QSO log (call, frequency, mode).
//  - Auto-detect (an option): a call seen 3 times right after DE or CQ in the decoded
//    text within 10 minutes near the same frequency (engine/callspot.h).
// Reports go out at most every 5 minutes, timed from when reporting was turned on.

#include "callspot.h"
#include "pskreporter.h"

#include <QDateTime>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

class QSettings;

class PskReporter : public QObject
{
    Q_OBJECT

public:
    explicit PskReporter(QSettings *settings, QObject *parent = nullptr);

    bool enabled() const
    {
        return m_enabled;
    }
    bool autoDetect() const
    {
        return m_auto;
    }
    bool testServer() const
    {
        return m_test;
    }
    QString antenna() const
    {
        return m_antenna;
    }
    // Ready to send: a call sign and a 4- or 6-character grid.
    bool configured() const;
    QString myCall() const
    {
        return m_myCall;
    }
    QString myGrid() const
    {
        return m_myGrid;
    }

    int spotsSent() const
    {
        return m_sent;
    }
    int waiting() const
    {
        return int(m_queue.waiting());
    }
    QDateTime nextSend() const;
    QString lastError() const
    {
        return m_error;
    }
    QString statusText() const;

    static bool validGrid(const QString &grid);
    // ADIF mode for a tab's text ("PSK" + "BPSK63" -> "PSK63"); empty = not reported.
    static QString modeFor(const QString &tab, const QString &subMode);

public slots:
    void setEnabled(bool on);
    void setAutoDetect(bool on);
    void setTestServer(bool on);
    void setAntenna(const QString &a);
    void setMyCall(const QString &call);
    void setMyGrid(const QString &grid);

    // Decoded (not sent) text of a tab, with the RF frequency of the signal.
    void feedText(const QString &tab, const QString &mode, const QString &text, qint64 rfHz);
    // A call to report now (e.g. a logged QSO).
    void addSpot(const QString &call, qint64 rfHz, const QString &mode);
    // Sends what is waiting (normally the timer does this).
    void sendNow();

signals:
    void statusChanged();
    void message(const QString &line); // for the TCI log

private:
    void save();
    void schedule();
    void transmit(const QHostAddress &to, quint16 port);

    QSettings *m_settings;
    bool m_enabled = false;
    bool m_auto = false;
    bool m_test = false;
    QString m_antenna;
    QString m_myCall, m_myGrid;

    callspot::Spotter m_spotter;
    pskreporter::Queue m_queue;
    QUdpSocket m_socket; // one source port for every datagram
    QTimer m_timer;
    qint64 m_nextSendMs = 0;
    quint32 m_randomId = 0;
    quint32 m_sequence = 0;
    int m_datagrams = 0; // since start: templates go in the first three ...
    qint64 m_templatesAtMs = 0; // ... and then once an hour
    int m_sent = 0;
    QString m_error;
    QHostAddress m_address;
    qint64 m_addressAtMs = 0;
};
