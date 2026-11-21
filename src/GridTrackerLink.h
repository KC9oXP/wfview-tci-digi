#pragma once

// GridTracker2 link: sends the UDP messages GridTracker reads from WSJT-X (same format) to
// 127.0.0.1:2237 by default (engine/gridtrackerudp.h).
//  - Heartbeat and Status every 15 s, and Status as soon as the dial, mode, call,
//    grid or transmit state changes: GridTracker shows the frequency and band.
//  - Decode: a call heard right after CQ, DE or 73 in the decoded text of the CW, RTTY,
//    PSK, Olivia/Contestia and AMTOR tabs, each call at most once a minute
//    ("CQ W1AW", "K1ABC W1AW", "DE W1AW"). Own call and sent text are never reported.
// Off until turned on (Options > GridTracker...). Nothing is read back.

#include "callspot.h"
#include "gridtrackerudp.h"

#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QStringList>
#include <QUdpSocket>

class QSettings;
class QWidget;

class GridTrackerLink : public QObject
{
    Q_OBJECT

public:
    explicit GridTrackerLink(QSettings *settings, QObject *parent = nullptr);

    bool enabled() const
    {
        return m_enabled;
    }
    QString host() const
    {
        return m_host.toString();
    }
    quint16 port() const
    {
        return m_port;
    }
    int decodesSent() const
    {
        return m_decodes;
    }

    void setEnabled(bool on);
    // Host must be an IP address (127.0.0.1, or a LAN address / multicast group).
    bool setTarget(const QString &host, quint16 port);

    // Station and radio state for Status (sent when anything changes).
    void setDial(qint64 hz);
    void setMode(const QString &mode);
    void setMyCall(const QString &call);
    void setMyGrid(const QString &grid);
    void setTransmitting(bool on);
    void setRxDf(double audioHz);

    // Received decoded text from a tab; audioHz is where the signal is in the passband,
    // rfHz the RF frequency, snr the measured SNR in dB (NaN if not known).
    void feedText(const QString &tab, const QString &mode, const QString &text, double audioHz, qint64 rfHz, double snr);

    void editSettings(QWidget *parent);

    // Every decode line sent, for the e2e test.
    QStringList sentMessages() const
    {
        return m_sentLines;
    }

signals:
    void message(const QString &text); // for the TCI log
    void statusChanged();

private:
    void sendStatusSoon();
    void sendStatus();
    void sendHeartbeat();
    void send(const std::vector<std::uint8_t> &bytes);
    void save();

    QSettings *m_settings;
    bool m_enabled = false;
    QHostAddress m_host{QHostAddress::LocalHost};
    quint16 m_port = 2237;
    QUdpSocket m_socket;
    QTimer m_beat; // 15 s heartbeat + status
    QTimer m_soon; // status after a change (short delay so a burst sends once)
    callspot::Spotter m_spotter;
    wsjtx::StatusInfo m_status;
    int m_decodes = 0;
    bool m_errorShown = false;
    QStringList m_sentLines;
};
