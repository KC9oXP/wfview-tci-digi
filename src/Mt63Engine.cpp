#include "Mt63Engine.h"

#include "extratxsources.h"

#include <QMetaType>
#include <QSettings>
#include <QThread>
#include <algorithm>
#include <cmath>

Mt63DecoderWorker::Mt63DecoderWorker(QObject *parent)
    : QObject(parent)
{
    m_statsTimer.start();
}

void Mt63DecoderWorker::pushAudio(QVector<float> mono, float sampleRate)
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
        emit statsUpdated(m_demod.snrDb(), m_demod.locked(), m_demod.freqOffsetHz());
    }
}

void Mt63DecoderWorker::setParams(
    int bandwidth, bool longInterleave, double centreHz, bool longIntegration, double squelchDb)
{
    mt63::Config c;
    c.bandwidth = bandwidth;
    c.longInterleave = longInterleave;
    c.centreHz = centreHz;
    c.longIntegration = longIntegration;
    m_demod.setSquelch(squelchDb);
    m_demod.setConfig(c);
}

void Mt63DecoderWorker::resetDecoder()
{
    m_demod.reset();
}

Mt63Engine::Mt63Engine(QObject *parent)
    : ModemEngine(QStringLiteral("MT63"), std::make_shared<Mt63TxSource>(), parent)
{
    setDecodeOnSend(true);
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_carrierHz = 1500.0;
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("mt63Decoder"));
    m_worker = new Mt63DecoderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &Mt63DecoderWorker::textDecoded, this, [this](const QString &t) { deliverText(t); });
    connect(m_worker, &Mt63DecoderWorker::statsUpdated, this, &Mt63Engine::onStats);
    m_thread->start(QThread::LowPriority);
    applyConfig();
}

Mt63Engine::~Mt63Engine()
{
    m_thread->quit();
    m_thread->wait();
}

void Mt63Engine::loadSettings(QSettings *s)
{
    if (s)
    {
        s->beginGroup(group());
        const int bw = s->value(QStringLiteral("Bandwidth"), m_bandwidth).toInt();
        m_bandwidth = (bw == 500 || bw == 2000) ? bw : 1000;
        m_long = s->value(QStringLiteral("LongInterleave"), m_long).toBool();
        m_squelch = std::clamp(s->value(QStringLiteral("Squelch"), m_squelch).toDouble(), 0.0, 20.0);
        if (!s->contains(QStringLiteral("Carrier")))
            m_carrierHz = 1500.0;
        s->endGroup();
    }
    ModemEngine::loadSettings(s);
}

mt63::Config Mt63Engine::config() const
{
    mt63::Config c;
    c.bandwidth = m_bandwidth;
    c.longInterleave = m_long;
    c.centreHz = m_carrierHz;
    return c;
}

QString Mt63Engine::modeName() const
{
    return QString::fromStdString(mt63::name(config()));
}

void Mt63Engine::applyConfig()
{
    // Keep the whole signal inside the passband.
    m_carrierHz = std::clamp(m_carrierHz, mt63::minCentreHz(m_bandwidth), mt63::maxCentreHz(m_bandwidth));
    QMetaObject::invokeMethod(m_worker,
                              "setParams",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_bandwidth),
                              Q_ARG(bool, m_long),
                              Q_ARG(double, m_carrierHz),
                              Q_ARG(bool, false),
                              Q_ARG(double, m_squelch));
    auto tx = std::static_pointer_cast<Mt63TxSource>(txSource());
    tx->setConfig(config());
    tx->setLevel(float(txLevel()));
}

void Mt63Engine::resetReceiver()
{
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
}

void Mt63Engine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!decoding())
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void Mt63Engine::setMode(int bandwidth, bool longInterleave)
{
    if (bandwidth != 500 && bandwidth != 1000 && bandwidth != 2000)
        return;
    if (bandwidth == m_bandwidth && longInterleave == m_long)
        return;
    if (transmitting())
        abortTx();
    m_bandwidth = bandwidth;
    m_long = longInterleave;
    save("Bandwidth", m_bandwidth);
    save("LongInterleave", m_long);
    reloadTxLevel();
    applyConfig();
    emit settingsChanged();
}

void Mt63Engine::setSquelch(double db)
{
    db = std::clamp(db, 0.0, 20.0);
    if (std::fabs(db - m_squelch) < 1e-6)
        return;
    m_squelch = db;
    save("Squelch", m_squelch);
    applyConfig();
    emit settingsChanged();
}

void Mt63Engine::onStats(double snrDb, bool locked, double offsetHz)
{
    if (!decoding())
        return;
    m_snrDb = snrDb;
    m_locked = locked;
    m_offset = offsetHz;
    emit statsChanged();
}
