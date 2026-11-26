#include "PskEngine.h"

#include "modemtxsource.h"

#include <QMetaType>
#include <QSettings>
#include <QThread>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int statsIntervalMs = 150;
constexpr double historySeconds = 5.0; // audio kept for click-to-tune and speed detection
constexpr double centreSeconds = 2.0; // the part used to find a signal's centre
} // namespace

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------

PskDecoderWorker::PskDecoderWorker(QObject *parent)
    : QObject(parent)
{
    m_statsTimer.start();
}

void PskDecoderWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    if (m_fs != sampleRate)
    {
        m_fs = sampleRate;
        m_demod.setSampleRate(m_fs);
        m_history.clear();
    }
    const std::string out = m_demod.process(mono.constData(), size_t(mono.size()));
    if (!out.empty())
        emit textDecoded(QString::fromLatin1(out.data(), int(out.size())));

    m_history.insert(m_history.end(), mono.constBegin(), mono.constEnd());
    const size_t keep = size_t(historySeconds * m_fs);
    if (m_history.size() > 2 * keep)
        m_history.erase(m_history.begin(), m_history.end() - std::ptrdiff_t(keep));

    if (m_statsTimer.elapsed() >= statsIntervalMs)
    {
        m_statsTimer.restart();
        const PskDemodulator::Stats s = m_demod.stats();
        emit statsUpdated(s.quality, s.levelDb, s.carrierHz, s.squelchOpen, m_generation);
        if (++m_rsqTick % 5 == 0)
            emit rsqUpdated(m_demod.rsq(), m_generation);
    }
}

void PskDecoderWorker::setParams(double baud, double carrierHz, bool afc, double squelch, int generation)
{
    m_baud = baud;
    m_generation = generation;
    m_demod.setBaud(baud);
    m_demod.setCarrier(carrierHz);
    m_demod.setAfc(afc);
    m_demod.setSquelch(squelch);
}

void PskDecoderWorker::resetDecoder()
{
    m_demod.reset();
}

void PskDecoderWorker::findCenter(double approxHz)
{
    const size_t keep = size_t(centreSeconds * m_fs);
    const size_t n = std::min(m_history.size(), keep);
    if (m_fs <= 0.0 || n < size_t(0.5 * m_fs))
    {
        emit centerFound(approxHz, approxHz);
        return;
    }
    const float *start = m_history.data() + (m_history.size() - n);
    emit centerFound(psk::findCenter(start, n, m_fs, approxHz, m_baud), approxHz);
}

void PskDecoderWorker::detectSpeed(double approxHz)
{
    const size_t keep = size_t(historySeconds * m_fs);
    const size_t n = std::min(m_history.size(), keep);
    psk::SpeedGuess g;
    if (m_fs > 0.0 && n >= size_t(2.0 * m_fs))
        g = psk::detectSpeed(m_history.data() + (m_history.size() - n), n, m_fs, approxHz);
    emit speedDetected(g.speed, approxHz, g.score[0], g.score[1], g.score[2]);
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

PskEngine::PskEngine(QObject *parent)
    : ModemEngine(QStringLiteral("PSK"), std::make_shared<PskTxSource>(), parent)
{
    setDecodeOnSend(true);
    qRegisterMetaType<QVector<float>>("QVector<float>");
    qRegisterMetaType<PskDemodulator::Rsq>("PskDemodulator::Rsq");
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("pskDecoder"));
    m_worker = new PskDecoderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &PskDecoderWorker::textDecoded, this, [this](const QString &t) { deliverText(t); });
    connect(m_worker, &PskDecoderWorker::statsUpdated, this, &PskEngine::onStats);
    connect(m_worker,
            &PskDecoderWorker::rsqUpdated,
            this,
            [this](const PskDemodulator::Rsq &r, int generation)
            {
                if (generation != m_generation || transmitting())
                    return;
                m_rsq = r;
                m_rsqTimer.start();
            });
    connect(m_worker, &PskDecoderWorker::centerFound, this, &PskEngine::onCenter);
    connect(m_worker, &PskDecoderWorker::speedDetected, this, &PskEngine::onSpeed);
    m_thread->start(QThread::LowPriority);
    applyConfig();
}

PskEngine::~PskEngine()
{
    m_thread->quit();
    m_thread->wait();
}

void PskEngine::loadSettings(QSettings *s)
{
    if (s)
    {
        s->beginGroup(group());
        m_speed = std::clamp(s->value(QStringLiteral("Speed"), m_speed).toInt(), 0, 2);
        m_afc = s->value(QStringLiteral("AFC"), m_afc).toBool();
        m_squelch = std::clamp(s->value(QStringLiteral("Squelch"), m_squelch).toDouble(), 0.0, 0.95);
        m_autoSpeed = s->value(QStringLiteral("AutoSpeed"), m_autoSpeed).toBool();
        s->endGroup();
    }
    ModemEngine::loadSettings(s); // applies
}

void PskEngine::applyConfig()
{
    ++m_generation;
    QMetaObject::invokeMethod(m_worker,
                              "setParams",
                              Qt::QueuedConnection,
                              Q_ARG(double, baud()),
                              Q_ARG(double, m_carrierHz),
                              Q_ARG(bool, m_afc),
                              Q_ARG(double, m_squelch),
                              Q_ARG(int, m_generation));
    auto tx = std::static_pointer_cast<PskTxSource>(txSource());
    tx->setBaud(baud());
    tx->setCarrier(m_carrierHz);
    tx->setLevel(float(txLevel()));
}

void PskEngine::resetReceiver()
{
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
}

void PskEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!decoding())
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void PskEngine::setSpeed(int speed)
{
    speed = std::clamp(speed, 0, 2);
    if (speed == m_speed)
        return;
    m_speed = speed;
    save("Speed", m_speed);
    reloadTxLevel();
    applyConfig();
    resetReceiver();
    emit settingsChanged();
}

void PskEngine::setAfc(bool on)
{
    if (on == m_afc)
        return;
    m_afc = on;
    save("AFC", m_afc);
    applyConfig();
    emit settingsChanged();
}

void PskEngine::setSquelch(double q)
{
    q = std::clamp(q, 0.0, 0.95);
    if (std::fabs(q - m_squelch) < 1e-6)
        return;
    m_squelch = q;
    save("Squelch", m_squelch);
    applyConfig();
    emit settingsChanged();
}

void PskEngine::setAutoSpeed(bool on)
{
    if (on == m_autoSpeed)
        return;
    m_autoSpeed = on;
    save("AutoSpeed", m_autoSpeed);
    emit settingsChanged();
}

void PskEngine::tuneTo(double hz)
{
    setCarrierHz(hz);
    if (!decoding())
        return;
    // Speed first: the centre is looked for within one baud of the right speed.
    if (m_autoSpeed && !transmitting())
        QMetaObject::invokeMethod(m_worker, "detectSpeed", Qt::QueuedConnection, Q_ARG(double, hz));
    QMetaObject::invokeMethod(m_worker, "findCenter", Qt::QueuedConnection, Q_ARG(double, hz));
}

void PskEngine::detectSpeed()
{
    if (!decoding())
    {
        setStatus(tr("Detect speed needs Decode on (it looks at the last 5 seconds of receive audio)."));
        return;
    }
    if (transmitting())
        return;
    m_speedAsked = true;
    setStatus(tr("Looking at the last 5 seconds at %1 Hz...").arg(qRound(m_carrierHz)));
    QMetaObject::invokeMethod(m_worker, "detectSpeed", Qt::QueuedConnection, Q_ARG(double, m_carrierHz));
}

void PskEngine::onSpeed(int speed, double approxHz, double s31, double s63, double s125)
{
    const bool asked = m_speedAsked;
    m_speedAsked = false;
    // Only if the user has not tuned somewhere else meanwhile.
    if (std::fabs(approxHz - m_carrierHz) > 1.5 * psk::baudFor(psk::Bpsk125) || transmitting())
        return;
    const QString lines = tr("baud-rate lines %1 / %2 / %3").arg(qRound(s31)).arg(qRound(s63)).arg(qRound(s125));
    if (speed < 0)
    {
        if (asked)
            setStatus(tr("No clear PSK speed at %1 Hz (%2; 60 needed). Tune onto the signal and give it 5 s.")
                          .arg(qRound(approxHz))
                          .arg(lines));
        return;
    }
    const bool changed = speed != m_speed;
    setSpeed(speed);
    if (changed || asked)
        setStatus(tr("Speed: %1 (%2)").arg(QString::fromLatin1(psk::speedName(speed)), lines));
}

void PskEngine::onCenter(double hz, double approxHz)
{
    // Only if the user has not tuned somewhere else meanwhile.
    if (std::fabs(approxHz - m_carrierHz) > 0.5 || std::fabs(hz - approxHz) > 1.5 * baud())
        return;
    setCarrierHz(hz);
    setStatus(tr("Tuned to %1 Hz").arg(hz, 0, 'f', 1));
}

void PskEngine::onStats(double quality, double levelDb, double carrierHz, bool squelchOpen, int generation)
{
    if (!decoding() || generation != m_generation)
        return;
    m_quality = quality;
    m_levelDb = levelDb;
    m_squelchOpen = squelchOpen;
    if (m_afc && !transmitting())
        trackCarrier(carrierHz);
    emit statsChanged();
}
