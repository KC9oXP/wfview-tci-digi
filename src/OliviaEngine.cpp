#include "OliviaEngine.h"

#include "extratxsources.h"

#include <QMetaType>
#include <QSettings>
#include <QThread>
#include <algorithm>
#include <cmath>

OliviaDecoderWorker::OliviaDecoderWorker(QObject *parent)
    : QObject(parent)
{
    m_statsTimer.start();
}

void OliviaDecoderWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    m_demod.setSampleRate(sampleRate);
    const std::string out = m_demod.process(mono.constData(), size_t(mono.size()));
    if (!out.empty())
        emit textDecoded(QString::fromLatin1(out.data(), int(out.size())));
    if (m_statsTimer.elapsed() >= 250)
    {
        m_statsTimer.restart();
        emit statsUpdated(m_demod.snr(), m_demod.freqOffsetHz());
    }
}

void OliviaDecoderWorker::setParams(bool contestia, int tones, int bandwidth, double centreHz, double squelch)
{
    olivia::Config c;
    c.contestia = contestia;
    c.tones = tones;
    c.bandwidth = bandwidth;
    c.centreHz = centreHz;
    m_demod.setSquelch(squelch);
    m_demod.setConfig(c);
}

void OliviaDecoderWorker::resetDecoder()
{
    m_demod.reset();
}

const QVector<OliviaEngine::Preset> &OliviaEngine::presets()
{
    static const QVector<Preset> p = {
        {false, 4, 125},
        {false, 4, 250},
        {false, 8, 250},
        {false, 8, 500},
        {false, 16, 500},
        {false, 32, 1000},
        {false, 64, 2000},
        {true, 4, 125},
        {true, 4, 250},
        {true, 8, 250},
        {true, 4, 500},
        {true, 8, 500},
        {true, 16, 500},
        {true, 8, 1000},
        {true, 16, 1000},
        {true, 32, 1000},
        {true, 64, 1000},
    };
    return p;
}

QString OliviaEngine::presetName(const Preset &p)
{
    return QStringLiteral("%1 %2/%3")
        .arg(p.contestia ? QStringLiteral("Contestia") : QStringLiteral("Olivia"))
        .arg(p.tones)
        .arg(p.bandwidth);
}

OliviaEngine::OliviaEngine(QObject *parent)
    : ModemEngine(QStringLiteral("Olivia"), std::make_shared<OliviaTxSource>(), parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_carrierHz = 1500.0;
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("oliviaDecoder"));
    m_worker = new OliviaDecoderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &OliviaDecoderWorker::textDecoded, this, [this](const QString &t) { deliverText(t); });
    connect(m_worker, &OliviaDecoderWorker::statsUpdated, this, &OliviaEngine::onStats);
    m_thread->start(QThread::LowPriority);
    applyConfig();
}

OliviaEngine::~OliviaEngine()
{
    m_thread->quit();
    m_thread->wait();
}

void OliviaEngine::loadSettings(QSettings *s)
{
    if (s)
    {
        s->beginGroup(group());
        m_contestia = s->value(QStringLiteral("Contestia"), m_contestia).toBool();
        m_tones = s->value(QStringLiteral("Tones"), m_tones).toInt();
        m_bandwidth = s->value(QStringLiteral("Bandwidth"), m_bandwidth).toInt();
        m_squelch = std::clamp(s->value(QStringLiteral("Squelch"), m_squelch).toDouble(), 0.0, 20.0);
        if (!s->contains(QStringLiteral("Carrier")))
            m_carrierHz = 1500.0;
        s->endGroup();
    }
    ModemEngine::loadSettings(s);
}

olivia::Config OliviaEngine::config() const
{
    olivia::Config c;
    c.contestia = m_contestia;
    c.tones = m_tones;
    c.bandwidth = m_bandwidth;
    c.centreHz = m_carrierHz;
    return c;
}

QString OliviaEngine::modeName() const
{
    return presetName({m_contestia, m_tones, m_bandwidth});
}

void OliviaEngine::applyConfig()
{
    // Keep the whole signal inside the passband.
    const double half = m_bandwidth / 2.0;
    if (m_carrierHz - half < kMinHz)
        m_carrierHz = kMinHz + half;
    if (m_carrierHz + half > kMaxHz)
        m_carrierHz = kMaxHz - half;
    QMetaObject::invokeMethod(m_worker,
                              "setParams",
                              Qt::QueuedConnection,
                              Q_ARG(bool, m_contestia),
                              Q_ARG(int, m_tones),
                              Q_ARG(int, m_bandwidth),
                              Q_ARG(double, m_carrierHz),
                              Q_ARG(double, m_squelch));
    auto tx = std::static_pointer_cast<OliviaTxSource>(txSource());
    tx->setConfig(config());
    tx->setLevel(float(txLevel()));
}

void OliviaEngine::resetReceiver()
{
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
}

void OliviaEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!decoding())
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void OliviaEngine::setMode(bool contestia, int tones, int bandwidth)
{
    if (contestia == m_contestia && tones == m_tones && bandwidth == m_bandwidth)
        return;
    if (transmitting())
        abortTx();
    m_contestia = contestia;
    m_tones = tones;
    m_bandwidth = bandwidth;
    save("Contestia", m_contestia);
    save("Tones", m_tones);
    save("Bandwidth", m_bandwidth);
    applyConfig();
    emit settingsChanged();
}

void OliviaEngine::setSquelch(double snr)
{
    snr = std::clamp(snr, 0.0, 20.0);
    if (std::fabs(snr - m_squelch) < 1e-6)
        return;
    m_squelch = snr;
    save("Squelch", m_squelch);
    applyConfig();
    emit settingsChanged();
}

void OliviaEngine::onStats(double snr, double offsetHz)
{
    if (!decoding())
        return;
    m_snr = snr;
    m_offset = offsetHz;
    emit statsChanged();
}
