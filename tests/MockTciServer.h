#pragma once

// A small TCI server that behaves like wfview 3.00's: the same start-up
// messages, a TX chrono frame before every RX audio frame, interleaved stereo
// RX audio with "channels = 1" in the header, and TX audio accepted only after
// the client sent trx:0,true. It plays an RTTY signal as receive audio and
// decodes whatever RTTY the client transmits. Since 0.2.0 the receive audio
// also carries a BPSK31 station at 1000 Hz and a Feld Hell station at 500 Hz,
// and transmitted BPSK31 at 1000 Hz is decoded too.

#include "modemtxsource.h"
#include "pskcore.h"
#include "rttycore.h"
#include "rttytxsource.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QWebSocketServer>
#include <memory>
#include <string>

class QWebSocket;

class MockTciServer : public QObject
{
    Q_OBJECT

public:
    explicit MockTciServer(QObject *parent = nullptr);
    bool listen(quint16 port = 0);
    quint16 port() const;

    // Stop RX audio (and so TX chrono) while transmitting, to exercise the
    // client's local TX clock.
    bool stopRxWhileTx = false;
    // Delay before reporting trx:0,true back (radio switching).
    int trxEchoMs = 80;

    std::string txDecoded; // RTTY decoded from the client's TX audio
    std::string txPskDecoded; // BPSK31 at 1000 Hz decoded from the client's TX audio
    quint64 txFramesReceived = 0;
    quint64 txFramesWhileNotTx = 0; // frames that arrived before trx:true (should be 0)
    bool transmitting = false;
    QString lastMode = QStringLiteral("digu");
    qint64 freq = 14080000;
    int txPackets = 0;

private slots:
    void onNewConnection();
    void onText(const QString &msg);
    void onBinary(const QByteArray &frame);
    void tick();

private:
    void sendRxPacket();
    void refillRx();
    void refillModes();

    std::unique_ptr<QWebSocketServer> m_server;
    QWebSocket *m_client = nullptr;
    bool m_streaming = false;
    QTimer m_timer;
    QElapsedTimer m_clock;
    quint64 m_framesSent = 0;
    RttyTxSource m_rxSignal; // receive audio: an RTTY station
    PskTxSource m_rxPsk; // ... a BPSK31 station
    HellTxSource m_rxHell; // ... and a Feld Hell station
    RttyDemodulator m_txDemod;
    PskDemodulator m_txPsk;
    RttyConfig m_cfg;
};
