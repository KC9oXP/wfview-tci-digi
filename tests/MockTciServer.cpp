#include "MockTciServer.h"

#include "tciprotocol.h"

#include <QWebSocket>
#include <cstring>
#include <random>
#include <vector>

namespace
{
constexpr uint32_t kFrames = 2048; // per RX packet, as wfview
std::mt19937 g_rng(11);
} // namespace

MockTciServer::MockTciServer(QObject *parent)
    : QObject(parent)
    , m_server(std::make_unique<QWebSocketServer>(QStringLiteral("mock"), QWebSocketServer::NonSecureMode))
{
    // Station on the air: USB, mark 2295, space 2125, 45.45 bd.
    m_cfg.sampleRate = 48000.0;
    m_cfg.markHz = 2295.0;
    m_cfg.reverse = true;
    m_rxSignal.setConfig(m_cfg);
    m_rxSignal.setLevel(0.4f);
    m_rxSignal.setDiddle(true);
    m_rxSignal.start(0.5);
    refillRx();
    refillModes();
    m_txDemod.setConfig(m_cfg);
    m_txDemod.setSquelch(0.0);

    m_rxPsk.setBaud(31.25);
    m_rxPsk.setCarrier(1000.0);
    m_rxPsk.setLevel(0.25f);
    m_rxPsk.start(1.0);
    m_rxHell.setCarrier(500.0);
    m_rxHell.setLevel(0.25f);
    m_rxHell.start(0.2);
    m_txPsk.setSampleRate(48000.0);
    m_txPsk.setBaud(31.25);
    m_txPsk.setCarrier(1000.0);
    m_txPsk.setSquelch(0.0);

    connect(m_server.get(), &QWebSocketServer::newConnection, this, &MockTciServer::onNewConnection);
    m_timer.setInterval(10);
    connect(&m_timer, &QTimer::timeout, this, &MockTciServer::tick);
}

bool MockTciServer::listen(quint16 port)
{
    return m_server->listen(QHostAddress::LocalHost, port);
}

quint16 MockTciServer::port() const
{
    return m_server->serverPort();
}

void MockTciServer::refillRx()
{
    m_rxSignal.queueText("RYRYRY CQ CQ DE W6EL W6EL W6EL K\n");
}

void MockTciServer::refillModes()
{
    if (m_rxPsk.idle())
        m_rxPsk.queueText("psk test de W6EL W6EL pse k\n");
    if (m_rxHell.idle())
        m_rxHell.queueText("HELL DE W6EL ");
}

void MockTciServer::onNewConnection()
{
    m_client = m_server->nextPendingConnection();
    connect(m_client, &QWebSocket::textMessageReceived, this, &MockTciServer::onText);
    connect(m_client, &QWebSocket::binaryMessageReceived, this, &MockTciServer::onBinary);
    connect(m_client,
            &QWebSocket::disconnected,
            this,
            [this]()
            {
                m_streaming = false;
                transmitting = false;
                m_timer.stop();
            });
    const char *init[] = {"protocol:WFVIEW,1.9;",
                          "device:IC-7610;",
                          "receive_only:false;",
                          "trx_count:2;",
                          "channels_count:2;",
                          "vfo_limits:30000,60000000;",
                          "if_limits:-19531,19531;",
                          "modulations_list:LSB,DIGL,USB,DIGU,AM,CW,RTTY,FM,CW-R,RTTY-R;",
                          "vfo:0,0,14080000;",
                          "modulation:0,digu;",
                          "trx:0,false;",
                          "iq_samplerate:48000;",
                          "audio_samplerate:48000;",
                          "audio_stream_sample_type:float32;",
                          "audio_stream_channels:2;",
                          "audio_stream_samples:2048;",
                          "tx_stream_audio_buffering:50;",
                          "start;",
                          "ready;"};
    for (const char *s : init)
        m_client->sendTextMessage(QString::fromLatin1(s));
}

void MockTciServer::onText(const QString &msg)
{
    for (const tci::Command &c : tci::parseCommands(msg.toStdString()))
    {
        if (c.name == "audio_start")
        {
            m_streaming = true;
            m_framesSent = 0;
            m_clock.start();
            m_timer.start();
        }
        else if (c.name == "audio_stop")
        {
            m_streaming = false;
        }
        else if (c.name == "trx" && c.args.size() >= 2)
        {
            transmitting = tci::toBool(c.args[1]);
            const QString echo = QStringLiteral("trx:0,%1;").arg(transmitting ? "true" : "false");
            QTimer::singleShot(transmitting ? trxEchoMs : 10,
                               this,
                               [this, echo]()
                               {
                                   if (m_client)
                                       m_client->sendTextMessage(echo);
                               });
            if (!transmitting)
            {
                m_txDemod.reset();
                m_txPsk.reset();
            }
        }
        else if (c.name == "modulation" && c.args.size() >= 2)
        {
            lastMode = QString::fromStdString(c.args[1]);
            m_client->sendTextMessage(QStringLiteral("modulation:0,%1;").arg(lastMode.toLower()));
        }
        else if (c.name == "vfo" && c.args.size() >= 3)
        {
            freq = QString::fromStdString(c.args[2]).toLongLong();
            m_client->sendTextMessage(QStringLiteral("vfo:0,0,%1;").arg(freq));
        }
    }
}

void MockTciServer::onBinary(const QByteArray &frame)
{
    tci::Header h;
    const auto *d = reinterpret_cast<const uint8_t *>(frame.constData());
    if (!tci::readHeader(d, size_t(frame.size()), h) || h.type != uint32_t(tci::StreamType::TxAudio))
        return;
    ++txPackets;
    std::vector<float> s, mono;
    if (!tci::readSamples(d, size_t(frame.size()), h, s))
        return;
    // wfview's TCI TX input is 48 kHz stereo float whatever the header says.
    tci::firstChannel(s, 2, mono);
    if (!transmitting)
    {
        txFramesWhileNotTx += mono.size();
        return;
    }
    txFramesReceived += mono.size();
    txDecoded += m_txDemod.process(mono.data(), mono.size());
    txPskDecoded += m_txPsk.process(mono.data(), mono.size());
}

void MockTciServer::tick()
{
    if (!m_streaming || !m_client)
        return;
    const quint64 due = quint64(m_clock.elapsed() * 48.0);
    while (m_framesSent + kFrames <= due)
    {
        if (!(stopRxWhileTx && transmitting))
            sendRxPacket();
        m_framesSent += kFrames;
    }
}

void MockTciServer::sendRxPacket()
{
    // TX chrono first (header only, length = 4096 floats, channels 1), as wfview.
    std::vector<uint8_t> chrono = tci::makeTxAudioFrame(0, 48000, 1, nullptr, 0);
    const uint32_t type = uint32_t(tci::StreamType::TxChrono), len = 4096;
    std::memcpy(chrono.data() + 24, &type, 4);
    std::memcpy(chrono.data() + 20, &len, 4);
    m_client->sendBinaryMessage(QByteArray(reinterpret_cast<const char *>(chrono.data()), int(chrono.size())));

    std::vector<float> stereo(kFrames * 2);
    m_rxSignal.render(stereo.data(), kFrames, 2, 48000.0f);
    if (m_rxSignal.idle())
        refillRx();
    std::vector<float> other(kFrames);
    m_rxPsk.render(other.data(), kFrames, 1, 48000.0f);
    for (size_t i = 0; i < kFrames; ++i)
    {
        stereo[2 * i] += other[i];
        stereo[2 * i + 1] += other[i];
    }
    m_rxHell.render(other.data(), kFrames, 1, 48000.0f);
    for (size_t i = 0; i < kFrames; ++i)
    {
        stereo[2 * i] += other[i];
        stereo[2 * i + 1] += other[i];
    }
    refillModes();
    std::normal_distribution<float> nd(0.0f, 0.05f);
    for (size_t i = 0; i < kFrames; ++i)
    {
        const float n = nd(g_rng);
        stereo[2 * i] += n;
        stereo[2 * i + 1] += n;
    }
    std::vector<uint8_t> f = tci::makeTxAudioFrame(0, 48000, 1, stereo.data(), stereo.size());
    const uint32_t rx = uint32_t(tci::StreamType::RxAudio);
    std::memcpy(f.data() + 24, &rx, 4);
    m_client->sendBinaryMessage(QByteArray(reinterpret_cast<const char *>(f.data()), int(f.size())));
}
