#pragma once

// TCI client: connects to wfview (or any TCI server) over a WebSocket, keeps
// the radio state it reports, delivers receive audio and sends transmit audio.
//
//   server --text--> state (vfo, modulation, trx, ...) --> signals
//   server --RX audio frame--> rxAudio(mono, rate)
//   server --TX chrono frame--> TxRender callback --> TX audio frame --> server
//
// Transmit audio is normally paced by the server's TX chrono frames. If they
// stop arriving while transmitting (some servers send them only alongside RX
// audio), a local clock keeps the audio flowing.

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <functional>
#include <memory>

class QWebSocket;

class TciClient : public QObject
{
    Q_OBJECT

public:
    // Fill frames * channels interleaved float samples at sampleRate.
    using TxRender = std::function<void(float *out, size_t frames, int channels, float sampleRate)>;

    enum class State
    {
        Disconnected,
        Connecting,
        Connected, // socket open, waiting for "ready;"
        Ready
    };

    explicit TciClient(QObject *parent = nullptr);
    ~TciClient() override;

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    State state() const
    {
        return m_state;
    }
    bool isReady() const
    {
        return m_state == State::Ready;
    }

    // Radio state as reported by the server (receiver 0, VFO A).
    QString protocol() const
    {
        return m_protocol;
    }
    QString device() const
    {
        return m_device;
    }
    qint64 frequencyHz() const
    {
        return m_freqHz;
    }
    QString modulation() const
    {
        return m_mode;
    }
    bool radioTransmitting() const
    {
        return m_radioTx;
    }
    bool receiveOnly() const
    {
        return m_receiveOnly;
    }
    QStringList modulations() const
    {
        return m_modulations;
    }

    void setModulation(const QString &mode);
    void setFrequency(qint64 hz);
    void sendText(const QString &commands); // raw "name:args;"

    // Transmit: setTransmit(true) sends trx:0,true and starts feeding audio
    // from the render callback; setTransmit(false) stops both.
    void setTxRender(TxRender render)
    {
        m_txRender = std::move(render);
    }
    void setTransmit(bool on);
    bool transmitRequested() const
    {
        return m_txOn;
    }
    quint64 txFramesSent() const
    {
        return m_txFrames;
    }

signals:
    void stateChanged(TciClient::State state, const QString &message);
    void radioInfoChanged(); // protocol, device, modulations, receive_only
    void frequencyChanged(qint64 hz);
    void modulationChanged(const QString &mode);
    void radioTransmitChanged(bool on);
    void rxAudio(QVector<float> mono, float sampleRate);
    // About once a second: a one-line summary of the audio streams.
    void diagnostics(const QString &text);
    void logLine(const QString &text);

private slots:
    void onConnected();
    void onDisconnected();
    void onText(const QString &message);
    void onBinary(const QByteArray &frame);
    void onTxTimer();
    void onDiagTimer();

private:
    void setState(State s, const QString &message = QString());
    void handleCommand(const std::string &name, const std::vector<std::string> &args);
    void sendTxFrames(size_t frames);
    void resetStreamStats();

    std::unique_ptr<QWebSocket> m_ws;
    State m_state = State::Disconnected;
    QUrl m_url;

    QString m_protocol;
    QString m_device;
    QStringList m_modulations;
    bool m_receiveOnly = false;
    qint64 m_freqHz = 0;
    QString m_mode;
    bool m_radioTx = false;

    int m_announcedChannels = 0;
    int m_announcedRate = 0;
    bool m_isWfview = false;

    // transmit
    TxRender m_txRender;
    bool m_txOn = false;
    int m_txChannels = 2;
    float m_txRate = 48000.0f;
    quint64 m_txFrames = 0;
    QElapsedTimer m_txClock;
    QElapsedTimer m_lastChrono;
    QTimer m_txTimer;
    std::vector<float> m_txBuf;

    // stream statistics for diagnostics()
    QTimer m_diagTimer;
    QElapsedTimer m_diagClock;
    quint64 m_rxFramesWin = 0;
    quint64 m_rxPacketsWin = 0;
    quint64 m_chronoWin = 0;
    quint64 m_txClockFramesWin = 0;
    quint64 m_txChronoFramesWin = 0;
    quint32 m_lastRxRate = 0;
    quint32 m_lastHeaderCh = 0;
    int m_lastUsedCh = 0;
    quint32 m_lastLength = 0;
    float m_rxPeak = 0.0f;
    bool m_headerMismatchLogged = false;
};
