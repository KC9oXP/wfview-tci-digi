#include "CwDecoder.h"

#include "cwdecodercore.h"
#include "cwmultidecoder.h"

#include <QMetaType>
#include <QThread>
#include <string>

namespace
{
constexpr float autoPitchLoHz = 200.0f;
constexpr float autoPitchHiHz = 1200.0f;
constexpr float multiLoHz = 200.0f;
// Above ~2 kHz the band beyond the radio's CW filter is near-silent, which
// lowers the noise floor estimate and lets noise through as stations.
constexpr float multiHiHz = 2000.0f;
constexpr int statsIntervalMs = 250;
constexpr int multiIntervalMs = 300;
} // namespace

// ---------------------------------------------------------------------------
// Worker (own QThread)
// ---------------------------------------------------------------------------

CwDecoderWorker::CwDecoderWorker(QObject *parent)
    : QObject(parent)
    , m_core(new CwDecoderCore())
{
    m_statsTimer.start();
    m_multiTimer.start();
}

CwDecoderWorker::~CwDecoderWorker() = default;

void CwDecoderWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;

    std::string out;
    m_core->feed(mono.constData(), static_cast<int>(mono.size()), sampleRate, out);
    if (!out.empty())
        emit textDecoded(QString::fromLatin1(out.data(), static_cast<int>(out.size())));

    if (m_multi)
    {
        m_multi->feed(mono.constData(), static_cast<int>(mono.size()), sampleRate);
        if (m_multiTimer.elapsed() >= multiIntervalMs)
        {
            m_multiTimer.restart();
            QVector<CwChannelInfo> list;
            for (const auto &c : m_multi->channels())
            {
                CwChannelInfo i;
                i.id = c.id;
                i.pitchHz = c.pitchHz;
                i.wpm = c.wpm;
                i.snrDb = c.snrDb;
                i.active = c.active;
                i.text = QString::fromLatin1(c.text.data(), static_cast<int>(c.text.size()));
                list.append(i);
            }
            emit multiUpdated(list);
        }
    }

    if (m_statsTimer.elapsed() >= statsIntervalMs)
    {
        m_statsTimer.restart();
        const CwDecoderCore::Stats s = m_core->stats();
        emit statsUpdated(s.pitchHz, s.speedWpm, s.snrDb, s.locked, s.open, m_core->sampleRate());
    }
}

void CwDecoderWorker::setPitch(bool fixed, float fixedHz)
{
    if (fixed)
        m_core->setFixedPitch(fixedHz);
    else
        m_core->setTrackRange(autoPitchLoHz, autoPitchHiHz, -1.0f);
}

void CwDecoderWorker::setSpeedWpm(float wpm)
{
    m_core->setSpeedWpm(wpm);
}

void CwDecoderWorker::setSquelch(bool on)
{
    m_core->setSquelch(on);
}

void CwDecoderWorker::setEngine(int engine)
{
    m_core->setEngine(engine == CwDecoder::EngineHandKey ? CwDecoderCore::Engine::Hand
                                                         : CwDecoderCore::Engine::Machine);
}

void CwDecoderWorker::resetDecoder()
{
    m_core->reset();
}

void CwDecoderWorker::setMulti(bool on)
{
    if (on && !m_multi)
    {
        m_multi.reset(new CwMultiDecoder());
        m_multi->setRange(multiLoHz, multiHiHz);
    }
    else if (!on && m_multi)
    {
        m_multi.reset();
        emit multiUpdated({});
    }
}

// ---------------------------------------------------------------------------
// Controller (GUI thread)
// ---------------------------------------------------------------------------

CwDecoder::CwDecoder(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    qRegisterMetaType<CwChannelInfo>("CwChannelInfo");
    qRegisterMetaType<QVector<CwChannelInfo>>("QVector<CwChannelInfo>");

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("cwDecoder"));
    m_worker = new CwDecoderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &CwDecoderWorker::textDecoded, this, &CwDecoder::onText);
    connect(m_worker, &CwDecoderWorker::statsUpdated, this, &CwDecoder::onStats);
    connect(m_worker, &CwDecoderWorker::multiUpdated, this, &CwDecoder::onMulti);
    m_thread->start(QThread::LowPriority);
    apply();
}

CwDecoder::~CwDecoder()
{
    m_thread->quit();
    m_thread->wait();
}

int CwDecoder::pitchMin()
{
    return static_cast<int>(autoPitchLoHz);
}

int CwDecoder::pitchMax()
{
    return static_cast<int>(CwDecoderCore::kMaxFixedPitchHz);
}

int CwDecoder::machineMaxPitch()
{
    return static_cast<int>(CwDecoderCore::kMaxMachinePitchHz);
}

void CwDecoder::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!m_enabled || m_paused)
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void CwDecoder::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    if (on)
    {
        QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
        m_appliedEngine = -1;
        apply();
        QMetaObject::invokeMethod(m_worker, "setSquelch", Qt::QueuedConnection, Q_ARG(bool, m_squelch));
        QMetaObject::invokeMethod(m_worker, "setMulti", Qt::QueuedConnection, Q_ARG(bool, m_multi));
        emit statusChanged(tr("Listening..."));
    }
    else
    {
        emit statusChanged(tr("Off"));
        emit channelsChanged({});
    }
    emit settingsChanged();
}

void CwDecoder::setPaused(bool on)
{
    m_paused = on;
}

void CwDecoder::setSquelch(bool on)
{
    if (m_squelch == on)
        return;
    m_squelch = on;
    QMetaObject::invokeMethod(m_worker, "setSquelch", Qt::QueuedConnection, Q_ARG(bool, on));
    emit settingsChanged();
}

void CwDecoder::setEngine(int engine)
{
    if ((engine != EngineMachine && engine != EngineHandKey) || engine == m_engine)
        return;
    m_engine = engine;
    apply();
    emit settingsChanged();
}

void CwDecoder::setMultiSignal(bool on)
{
    if (m_multi == on)
        return;
    m_multi = on;
    QMetaObject::invokeMethod(m_worker, "setMulti", Qt::QueuedConnection, Q_ARG(bool, on));
    if (!on)
        emit channelsChanged({});
    emit settingsChanged();
}

void CwDecoder::setFixedPitch(bool on)
{
    if (m_fixedPitch == on)
        return;
    m_fixedPitch = on;
    apply();
    emit settingsChanged();
}

void CwDecoder::setPitchHz(int hz)
{
    hz = qBound(pitchMin(), hz, pitchMax());
    if (m_pitchHz == hz)
        return;
    m_pitchHz = hz;
    apply();
    emit settingsChanged();
}

void CwDecoder::setFixedSpeed(bool on)
{
    if (m_fixedSpeed == on)
        return;
    m_fixedSpeed = on;
    apply();
    emit settingsChanged();
}

void CwDecoder::setSpeedWpm(int wpm)
{
    wpm = qBound(5, wpm, 60);
    if (m_speedWpm == wpm)
        return;
    m_speedWpm = wpm;
    apply();
    emit settingsChanged();
}

void CwDecoder::resync()
{
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
    apply();
}

void CwDecoder::decodeChannel(int pitchHz)
{
    if (pitchHz < pitchMin() || pitchHz > pitchMax())
        return;
    m_pitchHz = pitchHz;
    m_fixedPitch = true;
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
    apply();
    emit settingsChanged();
    emit textAppended(tr("\n[now decoding %1 Hz]\n").arg(pitchHz));
}

void CwDecoder::apply()
{
    const int engine = effectiveEngine();
    if (engine != m_appliedEngine)
    {
        m_appliedEngine = engine;
        QMetaObject::invokeMethod(m_worker, "setEngine", Qt::QueuedConnection, Q_ARG(int, engine));
    }
    QMetaObject::invokeMethod(
        m_worker, "setPitch", Qt::QueuedConnection, Q_ARG(bool, m_fixedPitch), Q_ARG(float, float(m_pitchHz)));
    const float wpm = m_fixedSpeed ? float(m_speedWpm) : -1.0f;
    QMetaObject::invokeMethod(m_worker, "setSpeedWpm", Qt::QueuedConnection, Q_ARG(float, wpm));
}

int CwDecoder::effectiveEngine() const
{
    // The Machine engine cannot tell a high pitch from the station it folds onto.
    if (m_engine == EngineMachine && m_fixedPitch && m_pitchHz > machineMaxPitch())
        return EngineHandKey;
    return m_engine;
}

void CwDecoder::onText(const QString &text)
{
    if (m_enabled && !m_paused)
        emit textAppended(text);
}

void CwDecoder::onStats(float pitchHz, float speedWpm, float snrDb, bool locked, bool squelchOpen, float sampleRate)
{
    if (!m_enabled)
        return;
    const QString source = m_fixedPitch ? tr("fixed") : (locked ? tr("locked") : tr("searching"));
    const QString engineNote
        = effectiveEngine() != m_engine ? tr("   (Hand key used above %1 Hz)").arg(machineMaxPitch()) : QString();
    const QString pitchText = pitchHz > 0.0f ? QString::number(qRound(pitchHz)) : QStringLiteral("--");
    emit statusChanged(tr("Pitch %1 Hz (%2)   Speed %3 WPM   Signal %4 dB%5   Audio %6 kHz")
                           .arg(pitchText)
                           .arg(source)
                           .arg(speedWpm, 0, 'f', 1)
                           .arg(snrDb, 0, 'f', 0)
                           .arg(squelchOpen ? QString() : tr(" (squelched)"))
                           .arg(sampleRate / 1000.0f, 0, 'f', 1)
                       + engineNote);
}

void CwDecoder::onMulti(const QVector<CwChannelInfo> &channels)
{
    emit channelsChanged((m_multi && m_enabled) ? channels : QVector<CwChannelInfo>());
}
