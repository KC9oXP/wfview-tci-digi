#include "PskEngine.h"

#include "modemtxsource.h"
#include "pskmultidecoder.h"

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
constexpr int multiIntervalMs = 300;
constexpr double multiLoHz = 200.0;
constexpr double multiHiHz = 3000.0;
} // namespace

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------

PskDecoderWorker::PskDecoderWorker(QObject *parent)
    : QObject(parent)
{
    m_statsTimer.start();
    m_multiTimer.start();
}

PskDecoderWorker::~PskDecoderWorker() = default;

void PskDecoderWorker::setMulti(bool on)
{
    if (on && !m_multi)
    {
        m_multi = std::make_unique<PskMultiDecoder>();
        m_multi->setRange(multiLoHz, multiHiHz);
        m_multi->setBaud(m_baud);
        m_multi->setSquelch(m_squelch, m_squelchDb);
    }
    else if (!on && m_multi)
    {
        m_multi.reset();
        emit multiUpdated({});
    }
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

    if (m_multi)
    {
        m_multi->feed(mono.constData(), size_t(mono.size()), m_fs);
        if (m_multiTimer.elapsed() >= multiIntervalMs)
        {
            m_multiTimer.restart();
            QVector<PskChannelInfo> list;
            for (const auto &c : m_multi->channels())
            {
                PskChannelInfo i;
                i.id = c.id;
                i.hz = c.hz;
                i.snrDb = c.snrDb;
                i.active = c.active;
                i.text = QString::fromLatin1(c.text.data(), int(c.text.size()));
                i.total = c.total;
                list.push_back(i);
            }
            emit multiUpdated(list);
        }
    }

    m_history.insert(m_history.end(), mono.constBegin(), mono.constEnd());
    const size_t keep = size_t(historySeconds * m_fs);
    if (m_history.size() > 2 * keep)
        m_history.erase(m_history.begin(), m_history.end() - std::ptrdiff_t(keep));

    if (m_statsTimer.elapsed() >= statsIntervalMs)
    {
        m_statsTimer.restart();
        const PskDemodulator::Stats s = m_demod.stats();
        emit statsUpdated(s.quality, s.levelDb, s.carrierHz, s.squelchLevel, s.squelchOpen, m_generation);
        if (++m_rsqTick % 5 == 0)
            emit rsqUpdated(m_demod.rsq(), m_generation);
    }
}

void PskDecoderWorker::setParams(
    double baud, double carrierHz, bool afc, double squelch, bool squelchDb, int generation)
{
    m_baud = baud;
    m_generation = generation;
    m_demod.setBaud(baud);
    m_demod.setCarrier(carrierHz);
    m_demod.setAfc(afc);
    m_demod.setSquelch(squelch, squelchDb);
    m_squelch = squelch;
    m_squelchDb = squelchDb;
    if (m_multi)
    {
        m_multi->setBaud(baud); // a new speed starts the list afresh
        m_multi->setSquelch(squelch, squelchDb);
    }
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
    qRegisterMetaType<PskChannelInfo>("PskChannelInfo");
    qRegisterMetaType<QVector<PskChannelInfo>>("QVector<PskChannelInfo>");
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
    connect(m_worker, &PskDecoderWorker::multiUpdated, this, &PskEngine::onMulti);
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
        m_sq.load(s);
        m_autoSpeed = s->value(QStringLiteral("AutoSpeed"), m_autoSpeed).toBool();
        m_multi = s->value(QStringLiteral("Multi"), m_multi).toBool();
        s->endGroup();
    }
    QMetaObject::invokeMethod(m_worker, "setMulti", Qt::QueuedConnection, Q_ARG(bool, m_multi));
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
                              Q_ARG(double, m_sq.value()),
                              Q_ARG(bool, m_sq.inDb),
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
    if (!m_sq.setValue(q))
        return;
    save(m_sq.inDb ? "SquelchDb" : "Squelch", m_sq.value());
    applyConfig();
    emit settingsChanged();
}

void PskEngine::setSquelchInDb(bool on)
{
    if (on == m_sq.inDb)
        return;
    m_sq.inDb = on;
    save("SquelchInDb", on);
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

void PskEngine::onStats(
    double quality, double levelDb, double carrierHz, double squelchLevel, bool squelchOpen, int generation)
{
    if (!decoding() || generation != m_generation)
        return;
    m_quality = quality;
    m_levelDb = levelDb;
    m_squelchOpen = squelchOpen;
    m_squelchLevel = squelchLevel;
    if (m_afc && !transmitting())
        trackCarrier(carrierHz);
    emit statsChanged();
}

void PskEngine::onDecodingChanged(bool on)
{
    ModemEngine::onDecodingChanged(on);
    if (!on)
    {
        m_channelSeen.clear();
        emit channelsChanged({});
    }
}

void PskEngine::setMultiSignal(bool on)
{
    if (on == m_multi)
        return;
    m_multi = on;
    save("Multi", m_multi);
    QMetaObject::invokeMethod(m_worker, "setMulti", Qt::QueuedConnection, Q_ARG(bool, on));
    if (!on)
    {
        m_channelSeen.clear();
        emit channelsChanged({});
    }
    emit settingsChanged();
}

void PskEngine::decodeChannel(double hz, const QString &history)
{
    if (hz < kMinHz || hz > kMaxHz)
        return;
    if (std::fabs(hz - m_pickedHz) < 0.5 && m_picked.isValid() && m_picked.elapsed() < 1500)
        return; // the second click of a double-click
    m_pickedHz = hz;
    m_picked.start();
    setCarrierHz(hz); // resets the receiver
    const QString h = history.trimmed();
    const int shown = qRound(hz);
    if (h.isEmpty())
        deliverText(tr("\n[now decoding %1 Hz]\n").arg(shown));
    else
        deliverText(tr("\n[now decoding %1 Hz; copied so far:]\n%2\n").arg(shown).arg(h));
    setStatus(tr("Decoding %1 Hz from the multi-signal list").arg(shown));
}

void PskEngine::onMulti(const QVector<PskChannelInfo> &channels)
{
    const bool on = m_multi && decoding();
    if (!on)
    {
        m_channelSeen.clear();
        emit channelsChanged({});
        return;
    }
    if (transmitting())
        return; // keep the list as it was; our own signal is not a station
    emit channelsChanged(channels);
    // Pass each row's new text on (call sign spotting); forget closed rows.
    QHash<int, quint64> seen;
    for (const PskChannelInfo &c : channels)
    {
        const quint64 before = m_channelSeen.value(c.id, 0);
        seen.insert(c.id, c.total);
        if (c.total <= before)
            continue;
        const int fresh = int(qMin<quint64>(c.total - before, quint64(c.text.size())));
        emit channelText(c.id, c.hz, c.text.right(fresh));
    }
    m_channelSeen = seen;
}
