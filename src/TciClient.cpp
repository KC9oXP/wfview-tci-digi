#include "TciClient.h"

#include "tciprotocol.h"

#include <QWebSocket>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int txTimerMs = 20;
constexpr qint64 chronoStaleMs = 150; // no TX chrono for this long: pace TX audio locally
constexpr double txLeadSeconds = 0.06; // local pacing keeps this much audio queued at the server
constexpr size_t maxFramesPerSend = 2048;

std::string toStd(const QString &s)
{
    return s.toStdString();
}
} // namespace

TciClient::TciClient(QObject *parent)
    : QObject(parent)
    , m_ws(std::make_unique<QWebSocket>(QString(), QWebSocketProtocol::VersionLatest))
{
    connect(m_ws.get(), &QWebSocket::connected, this, &TciClient::onConnected);
    connect(m_ws.get(), &QWebSocket::disconnected, this, &TciClient::onDisconnected);
    connect(m_ws.get(), &QWebSocket::textMessageReceived, this, &TciClient::onText);
    connect(m_ws.get(), &QWebSocket::binaryMessageReceived, this, &TciClient::onBinary);
    connect(m_ws.get(),
            &QWebSocket::errorOccurred,
            this,
            [this](QAbstractSocket::SocketError)
            {
                if (m_state == State::Connecting)
                    setState(State::Disconnected, tr("Could not connect: %1").arg(m_ws->errorString()));
            });

    m_txTimer.setInterval(txTimerMs);
    connect(&m_txTimer, &QTimer::timeout, this, &TciClient::onTxTimer);
    m_diagTimer.setInterval(1000);
    connect(&m_diagTimer, &QTimer::timeout, this, &TciClient::onDiagTimer);
}

TciClient::~TciClient()
{
    if (m_txOn && m_ws->state() == QAbstractSocket::ConnectedState)
        m_ws->sendTextMessage(QStringLiteral("trx:0,false;"));
    m_ws->disconnect(this);
    m_ws->abort();
}

void TciClient::connectToServer(const QString &host, quint16 port)
{
    disconnectFromServer();
    m_url = QUrl(QStringLiteral("ws://%1:%2").arg(host.trimmed()).arg(port));
    setState(State::Connecting, tr("Connecting to %1 ...").arg(m_url.toString()));
    m_ws->open(m_url);
}

void TciClient::disconnectFromServer()
{
    if (m_txOn)
        setTransmit(false);
    if (m_ws->state() != QAbstractSocket::UnconnectedState)
        m_ws->close();
    m_diagTimer.stop();
}

void TciClient::setState(State s, const QString &message)
{
    m_state = s;
    emit stateChanged(s, message);
}

void TciClient::onConnected()
{
    m_protocol.clear();
    m_device.clear();
    m_modulations.clear();
    m_announcedChannels = 0;
    m_announcedRate = 0;
    m_isWfview = false;
    m_headerMismatchLogged = false;
    resetStreamStats();
    m_diagTimer.start();
    setState(State::Connected, tr("Connected, waiting for the radio state..."));
}

void TciClient::onDisconnected()
{
    const bool wasTx = m_txOn;
    m_txOn = false;
    m_txTimer.stop();
    m_diagTimer.stop();
    if (m_radioTx)
    {
        m_radioTx = false;
        emit radioTransmitChanged(false);
    }
    if (m_state != State::Disconnected)
        setState(State::Disconnected,
                 wasTx ? tr("Disconnected while transmitting (the server releases PTT when a client drops).")
                       : tr("Disconnected"));
}

void TciClient::sendText(const QString &commands)
{
    if (m_ws->state() == QAbstractSocket::ConnectedState)
        m_ws->sendTextMessage(commands);
}

void TciClient::setModulation(const QString &mode)
{
    sendText(QString::fromStdString(tci::makeCommand("modulation", {"0", toStd(mode.toLower())})));
}

void TciClient::setFrequency(qint64 hz)
{
    sendText(QString::fromStdString(tci::makeCommand("vfo", {"0", "0", std::to_string(hz)})));
}

// ---------------------------------------------------------------------------
// text
// ---------------------------------------------------------------------------

void TciClient::onText(const QString &message)
{
    for (const tci::Command &c : tci::parseCommands(message.toStdString()))
        handleCommand(c.name, c.args);
}

void TciClient::handleCommand(const std::string &name, const std::vector<std::string> &args)
{
    const auto arg = [&args](size_t i) { return i < args.size() ? args[i] : std::string(); };
    const auto qarg = [&arg](size_t i) { return QString::fromStdString(arg(i)); };

    if (name == "protocol")
    {
        m_protocol = qarg(0) + (args.size() > 1 ? QStringLiteral(" ") + qarg(1) : QString());
        m_isWfview = tci::toLower(arg(0)) == "wfview";
        emit radioInfoChanged();
    }
    else if (name == "device")
    {
        m_device = qarg(0);
        emit radioInfoChanged();
    }
    else if (name == "receive_only")
    {
        m_receiveOnly = tci::toBool(arg(0));
        emit radioInfoChanged();
    }
    else if (name == "modulations_list")
    {
        m_modulations.clear();
        for (const std::string &m : args)
            if (!m.empty())
                m_modulations << QString::fromStdString(tci::toUpper(m));
        emit radioInfoChanged();
    }
    else if (name == "vfo" && args.size() >= 3)
    {
        if (arg(0) == "0" && arg(1) == "0")
        {
            const qint64 hz = qarg(2).toLongLong();
            if (hz != m_freqHz)
            {
                m_freqHz = hz;
                emit frequencyChanged(hz);
            }
        }
    }
    else if (name == "modulation" && args.size() >= 2)
    {
        if (arg(0) == "0")
        {
            const QString m = qarg(1).toUpper();
            if (m != m_mode)
            {
                m_mode = m;
                emit modulationChanged(m);
            }
        }
    }
    else if (name == "trx" && args.size() >= 2)
    {
        if (arg(0) == "0")
        {
            const bool on = tci::toBool(arg(1));
            if (on != m_radioTx)
            {
                m_radioTx = on;
                emit radioTransmitChanged(on);
            }
        }
    }
    else if (name == "tx_sensors" && args.size() >= 5)
    {
        if (arg(0) == "0")
            emit txSensors(qarg(1).toDouble(), qarg(2).toDouble(), qarg(4).toDouble());
    }
    else if (name == "audio_stream_channels")
    {
        m_announcedChannels = qarg(0).toInt();
    }
    else if (name == "audio_samplerate")
    {
        m_announcedRate = qarg(0).toInt();
    }
    else if (name == "ready")
    {
        // Ask for what wfview sends anyway; other TCI servers honour it.
        sendText(QStringLiteral("audio_samplerate:48000;"));
        sendText(QStringLiteral("audio_stream_sample_type:float32;"));
        sendText(QStringLiteral("audio_stream_channels:2;"));
        sendText(QStringLiteral("audio_start:0;"));
        // Power/ALC/SWR while transmitting. The TCI spec is "enable,interval_ms";
        // wfview 3.00 reads the enable flag from the second argument instead.
        sendText(m_isWfview ? QStringLiteral("tx_sensors_enable:0,true;")
                            : QStringLiteral("tx_sensors_enable:true,200;"));
        setState(State::Ready, tr("Connected to %1").arg(m_device.isEmpty() ? m_url.toString() : m_device));
    }
}

// ---------------------------------------------------------------------------
// binary
// ---------------------------------------------------------------------------

void TciClient::onBinary(const QByteArray &frame)
{
    const auto *data = reinterpret_cast<const uint8_t *>(frame.constData());
    const size_t size = static_cast<size_t>(frame.size());
    tci::Header h;
    if (!tci::readHeader(data, size, h))
        return;

    if (h.type == uint32_t(tci::StreamType::RxAudio))
    {
        std::vector<float> samples;
        if (!tci::readSamples(data, size, h, samples) || samples.empty())
            return;
        const int ch = tci::effectiveChannels(h, m_announcedChannels, m_isWfview);
        if (!m_headerMismatchLogged && int(h.channels) != ch)
        {
            m_headerMismatchLogged = true;
            emit logLine(tr("Note: RX audio header says %1 channel(s) but the server announced %2; using %2.")
                             .arg(h.channels)
                             .arg(ch));
        }
        std::vector<float> mono;
        tci::firstChannel(samples, ch, mono);
        const float rate
            = h.sampleRate > 0 ? float(h.sampleRate) : (m_announcedRate > 0 ? float(m_announcedRate) : 48000.0f);

        ++m_rxPacketsWin;
        m_rxFramesWin += mono.size();
        m_lastRxRate = uint32_t(rate);
        m_lastHeaderCh = h.channels;
        m_lastUsedCh = ch;
        m_lastLength = h.length;
        for (float v : mono)
            m_rxPeak = std::max(m_rxPeak, std::fabs(v));

        emit rxAudio(QVector<float>(mono.begin(), mono.end()), rate);
        return;
    }

    if (h.type == uint32_t(tci::StreamType::TxChrono))
    {
        ++m_chronoWin;
        m_lastChrono.start();
        if (!m_txOn)
            return;
        // length counts all channels.
        size_t frames = h.length / uint32_t(std::max(1, m_txChannels));
        if (h.sampleRate > 0)
            m_txRate = float(h.sampleRate);
        frames = std::min(frames, maxFramesPerSend);
        if (frames == 0)
            frames = maxFramesPerSend;
        m_txChronoFramesWin += frames;
        sendTxFrames(frames);
    }
}

// ---------------------------------------------------------------------------
// transmit
// ---------------------------------------------------------------------------

void TciClient::setTransmit(bool on)
{
    if (on == m_txOn)
        return;
    if (on && m_state != State::Ready)
        return;
    m_txOn = on;
    sendText(QStringLiteral("trx:0,%1;").arg(on ? QStringLiteral("true") : QStringLiteral("false")));
    if (on)
    {
        m_txFrames = 0;
        m_txClock.start();
        m_txTimer.start();
    }
    else
    {
        m_txTimer.stop();
    }
}

void TciClient::onTxTimer()
{
    if (!m_txOn)
        return;
    // Normal case: the server's TX chrono frames pace the audio.
    if (m_lastChrono.isValid() && m_lastChrono.elapsed() < chronoStaleMs)
        return;
    const double target = (m_txClock.elapsed() / 1000.0 + txLeadSeconds) * m_txRate;
    while (double(m_txFrames) < target)
    {
        const size_t n = std::min(maxFramesPerSend, size_t(std::ceil(target - double(m_txFrames))));
        m_txClockFramesWin += n;
        sendTxFrames(n);
    }
}

void TciClient::sendTxFrames(size_t frames)
{
    if (frames == 0 || m_ws->state() != QAbstractSocket::ConnectedState)
        return;
    const size_t n = frames * size_t(m_txChannels);
    m_txBuf.assign(n, 0.0f);
    if (m_txRender)
        m_txRender(m_txBuf.data(), frames, m_txChannels, m_txRate);
    const std::vector<uint8_t> f
        = tci::makeTxAudioFrame(0, uint32_t(m_txRate), uint32_t(m_txChannels), m_txBuf.data(), n);
    m_ws->sendBinaryMessage(QByteArray(reinterpret_cast<const char *>(f.data()), int(f.size())));
    m_txFrames += frames;
}

// ---------------------------------------------------------------------------
// diagnostics
// ---------------------------------------------------------------------------

void TciClient::resetStreamStats()
{
    m_diagClock.start();
    m_rxFramesWin = m_rxPacketsWin = m_chronoWin = 0;
    m_txClockFramesWin = m_txChronoFramesWin = 0;
    m_rxPeak = 0.0f;
}

void TciClient::onDiagTimer()
{
    const double sec = std::max(0.001, m_diagClock.elapsed() / 1000.0);
    QString t;
    if (m_rxPacketsWin == 0)
    {
        t = tr("RX audio: none");
    }
    else
    {
        const double peakDb = 20.0 * std::log10(std::max(1e-6f, m_rxPeak));
        t = tr("RX audio: %1 Hz, %2 ch (header %3), %4 frames/packet, %5 packets/s, %6 frames/s, peak %7 dBFS")
                .arg(m_lastRxRate)
                .arg(m_lastUsedCh)
                .arg(m_lastHeaderCh)
                .arg(m_lastUsedCh > 0 ? m_lastLength / quint32(m_lastUsedCh) : m_lastLength)
                .arg(m_rxPacketsWin / sec, 0, 'f', 1)
                .arg(qRound64(m_rxFramesWin / sec))
                .arg(peakDb, 0, 'f', 1);
    }
    t += tr("   TX chrono: %1/s").arg(m_chronoWin / sec, 0, 'f', 1);
    if (m_txOn)
    {
        t += tr("   TX audio: %1 frames/s (%2)")
                 .arg(qRound64((m_txChronoFramesWin + m_txClockFramesWin) / sec))
                 .arg(m_txClockFramesWin > m_txChronoFramesWin ? tr("local clock") : tr("server chrono"));
    }
    emit diagnostics(t);
    resetStreamStats();
}
