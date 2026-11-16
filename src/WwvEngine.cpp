#include "WwvEngine.h"

#include "AppPaths.h"
#include "ClockSet.h"
#include "RadioModes.h"

#include <QDate>
#include <QSettings>
#include <QThread>
#include <QTime>
#include <QTimeZone>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int kSettleMs = 2000;
constexpr int kMeasureMs = 10000; // each sideband
constexpr qint64 kStandards[] = {2500000, 5000000, 10000000, 15000000, 20000000, 25000000};

qint64 epochMs(const wwv::Time &t, int second)
{
    const QDate d = QDate(t.year, 1, 1).addDays(t.dayOfYear - 1);
    return QDateTime(d, QTime(t.hour, t.minute, 0), QTimeZone::UTC).toMSecsSinceEpoch() + qint64(second) * 1000;
}
} // namespace

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------

void WwvWorker::pushAudio(QVector<float> mono, float sampleRate, double arrivalEpochMs, bool live)
{
    const qint64 arrivalMs = qint64(arrivalEpochMs);
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    if (double(sampleRate) != m_fs)
    {
        m_fs = double(sampleRate);
        m_dec.setSampleRate(m_fs);
        clearTiming();
    }
    m_dec.process(mono.constData(), size_t(mono.size()));

    WwvSnapshot s;
    s.st = m_dec.status();
    if (!live || m_dec.carrierMode())
    {
        clearTiming();
    }
    else
    {
        // When was each sample heard by this computer? A block arrives late by
        // whatever the network and the queues add, never early: the smallest
        // (arrival - sample time) of the last seconds is the real figure.
        const double c = double(arrivalMs) - m_dec.samplesProcessed() / m_fs * 1000.0;
        if (!m_arrivals.empty() && std::fabs(c - m_arrivals.back().second) > 1500.0)
            clearTiming(); // audio was interrupted
        m_arrivals.emplace_back(arrivalMs, c);
        while (!m_arrivals.empty() && arrivalMs - m_arrivals.front().first > 6000)
            m_arrivals.pop_front();
        double cmin = m_arrivals.front().second;
        for (const auto &a : m_arrivals)
            cmin = std::min(cmin, a.second);
        const bool settled = arrivalMs - m_arrivals.front().first > 3000;
        if (s.st.timeValid && settled && s.st.tickSample != m_lastTick)
        {
            m_lastTick = s.st.tickSample;
            const double pcAtTick = cmin + s.st.tickSample / m_fs * 1000.0;
            m_raw.push_back(pcAtTick - double(epochMs(s.st.time, s.st.secondInMinute)));
            while (m_raw.size() > 30)
                m_raw.pop_front();
        }
        if (!s.st.timeValid)
            m_raw.clear();
        if (m_raw.size() >= 5)
        {
            std::vector<double> v(m_raw.begin(), m_raw.end());
            std::sort(v.begin(), v.end());
            s.rawValid = true;
            s.rawOffsetMs = v[v.size() / 2];
            s.rawSpreadMs = v[v.size() * 3 / 4] - v[v.size() / 4];
            s.rawCount = int(v.size());
        }
    }
    if (s.st.timeValid)
        s.utcTickMs = epochMs(s.st.time, s.st.secondInMinute);
    // About five a second, by this thread's own clock: blocks can arrive in a
    // burst (a recording played fast, a backlog) with nearly the same arrival time.
    // A lamp changing or a new frame is never held back, or the last block of a
    // burst could leave the tab showing the state from before it.
    const int key = int(s.st.ticks) | int(s.st.tones) << 1 | int(s.st.code) << 2 | int(s.st.timeValid) << 3
        | int(s.st.timeVerified) << 4 | int(s.st.carrier) << 5 | int(s.st.hawaii) << 6 | s.st.framesDecoded << 8;
    if (!m_emitTimer.isValid() || m_emitTimer.elapsed() >= 200 || key != m_emitKey)
    {
        m_emitKey = key;
        m_emitTimer.restart();
        emit snapshot(s);
    }
}

void WwvWorker::setCarrierMode(bool on)
{
    m_dec.setCarrierMode(on);
    clearTiming();
}

void WwvWorker::resetDecoder()
{
    m_dec.reset();
    clearTiming();
}

void WwvWorker::clearTiming()
{
    m_arrivals.clear();
    m_raw.clear();
    m_lastTick = -1.0;
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

WwvEngine::WwvEngine(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    qRegisterMetaType<WwvSnapshot>("WwvSnapshot");
    m_grid = m_settings->value(QStringLiteral("WWV/Grid")).toString();
    m_audioDelayMs = m_settings->value(delayKey(), 0.0).toDouble();
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("wwvDecoder"));
    m_worker = new WwvWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker,
            &WwvWorker::snapshot,
            this,
            [this](const WwvSnapshot &s)
            {
                if (!m_decoding && m_carrierStage == 0)
                    return;
                m_snap = s;
                emit statsChanged();
            });
    m_thread->start(QThread::LowPriority);

    m_carrierTimer.setSingleShot(true);
    connect(&m_carrierTimer,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (m_carrierStage == 1 || m_carrierStage == 3)
                {
                    // the radio has settled on the new frequency: measure
                    ++m_carrierStage;
                    QMetaObject::invokeMethod(m_worker, "setCarrierMode", Qt::QueuedConnection, Q_ARG(bool, true));
                    m_carrierEndMs = QDateTime::currentMSecsSinceEpoch() + kMeasureMs;
                    m_carrierTimer.start(kMeasureMs);
                    emit statsChanged();
                }
                else if (m_carrierStage == 2)
                {
                    // upper sideband done: keep it, then the same from the other side
                    checkRadioFollowed(m_nominalHz - 1000, false);
                    m_usbOk = m_snap.st.carrier && m_snap.st.carrierSeconds >= 4.0;
                    m_usbHz = m_snap.st.carrierHz - 1000.0;
                    m_usbSeconds = m_snap.st.carrierSeconds;
                    QMetaObject::invokeMethod(m_worker, "setCarrierMode", Qt::QueuedConnection, Q_ARG(bool, false));
                    m_snap.st.carrier = false;
                    m_snap.st.carrierSeconds = 0.0;
                    if (!m_usbOk)
                    {
                        finishCarrier(true);
                        return;
                    }
                    m_carrierStage = 3;
                    emit tuneForCarrier(m_nominalHz + 1000, true);
                    m_carrierTimer.start(kSettleMs);
                    emit statsChanged();
                }
                else if (m_carrierStage == 4)
                {
                    checkRadioFollowed(m_nominalHz + 1000, true);
                    finishCarrier(true);
                }
            });
}

WwvEngine::~WwvEngine()
{
    m_thread->quit();
    m_thread->wait();
}

QString WwvEngine::delayKey() const
{
    const QString dev = AppPaths::safe(m_device.trimmed());
    return dev.isEmpty() ? QStringLiteral("WWV/AudioDelayMs") : QStringLiteral("WWV/AudioDelayMs-") + dev;
}

qint64 WwvEngine::nearestStandard(qint64 hz)
{
    for (qint64 f : kStandards)
        if (qAbs(hz - f) <= 6000)
            return f;
    return 0;
}

void WwvEngine::setMessage(const QString &m)
{
    if (m == m_message)
        return;
    m_message = m;
    emit messageChanged(m);
}

void WwvEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!m_decoding && m_carrierStage == 0)
        return;
    QMetaObject::invokeMethod(m_worker,
                              "pushAudio",
                              Qt::QueuedConnection,
                              Q_ARG(QVector<float>, mono),
                              Q_ARG(float, sampleRate),
                              Q_ARG(double, double(QDateTime::currentMSecsSinceEpoch())),
                              Q_ARG(bool, m_live));
}

void WwvEngine::setDecoding(bool on)
{
    if (on == m_decoding)
        return;
    m_decoding = on;
    if (on)
        QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
    else if (m_carrierStage == 0)
        m_snap = WwvSnapshot();
    emit settingsChanged();
    emit statsChanged();
}

void WwvEngine::setGrid(const QString &grid)
{
    const QString g = grid.trimmed();
    if (g == m_grid)
        return;
    m_grid = g;
    m_settings->setValue(QStringLiteral("WWV/Grid"), g);
    emit settingsChanged();
    emit statsChanged();
}

bool WwvEngine::pathDelay(double &ms, double &km) const
{
    double lat = 0, lon = 0;
    if (!wwv::gridToLatLon(m_grid.toStdString(), lat, lon))
        return false;
    ms = wwv::pathDelayMs(lat, lon, m_snap.st.hawaii, &km);
    return true;
}

void WwvEngine::setAudioDelayMs(double ms)
{
    ms = std::clamp(ms, -5000.0, 5000.0);
    if (std::fabs(ms - m_audioDelayMs) < 0.05)
        return;
    m_audioDelayMs = ms;
    m_settings->setValue(delayKey(), ms);
    emit settingsChanged();
    emit statsChanged();
}

void WwvEngine::setLive(bool live)
{
    m_live = live;
}

void WwvEngine::setRadio(bool ready, qint64 hz, const QString &mode, const QString &device)
{
    m_ready = ready;
    m_radioHz = hz;
    m_radioMode = mode;
    if (device != m_device)
    {
        m_device = device;
        // the audio delay belongs to the radio (and how wfview reaches it)
        m_audioDelayMs
            = m_settings->value(delayKey(), m_settings->value(QStringLiteral("WWV/AudioDelayMs"), 0.0)).toDouble();
        emit settingsChanged();
    }
    if (!ready && m_carrierStage != 0)
        finishCarrier(false);
}

bool WwvEngine::pcOffsetMs(double &ms) const
{
    if (!m_live || !m_snap.rawValid || !m_snap.st.timeVerified)
        return false;
    double path = 0, km = 0;
    pathDelay(path, km); // 0 without a grid
    ms = m_snap.rawOffsetMs - path - m_audioDelayMs;
    return true;
}

void WwvEngine::calibrateDelay()
{
    if (!m_live || !m_snap.rawValid || !m_snap.st.timeVerified)
    {
        setMessage(tr("Calibrate delay: wait until the time is decoded and verified (the Time lamp)."));
        return;
    }
    double path = 0, km = 0;
    pathDelay(path, km);
    setAudioDelayMs(m_snap.rawOffsetMs - path);
    setMessage(tr("Audio delay set to %1 ms for %2 (taking the PC clock as correct right now).")
                   .arg(m_audioDelayMs, 0, 'f', 0)
                   .arg(m_device.isEmpty() ? tr("this radio") : m_device));
}

void WwvEngine::setPcClock()
{
    double off = 0;
    if (!pcOffsetMs(off))
    {
        setMessage(tr("Set PC clock: wait until the time is decoded and verified (the Time lamp)."));
        return;
    }
    QString err;
    const qint64 shift = -qint64(std::llround(off));
    if (!ClockSet::requestAdjust(shift, &err))
    {
        setMessage(tr("PC clock not changed: %1").arg(err));
        return;
    }
    QMetaObject::invokeMethod(m_worker, "clearTiming", Qt::QueuedConnection);
    m_snap.rawValid = false;
    setMessage(tr("PC clock moved %1 %2 s onto WWV. It is being measured again now.")
                   .arg(shift >= 0 ? tr("forward") : tr("back"))
                   .arg(std::fabs(double(shift)) / 1000.0, 0, 'f', 3));
    emit statsChanged();
}

int WwvEngine::carrierSecondsLeft() const
{
    if (m_carrierStage == 0)
        return 0;
    // what is still to come after the running timer
    static const int after[] = {0, 2 * kMeasureMs + kSettleMs, kMeasureMs + kSettleMs, kMeasureMs, 0};
    const qint64 left = after[m_carrierStage] + m_carrierTimer.remainingTime();
    return int((left + 999) / 1000);
}

void WwvEngine::measureCarrier()
{
    if (m_carrierStage != 0)
        return;
    if (!m_ready || m_radioHz <= 0)
    {
        setMessage(tr("Measure carrier: connect to wfview first."));
        return;
    }
    if (!m_live)
    {
        setMessage(tr("Measure carrier: stop the WAV playback first."));
        return;
    }
    m_nominalHz = nearestStandard(m_radioHz);
    if (m_nominalHz == 0)
    {
        setMessage(tr("Measure carrier: tune to WWV first (2.5, 5, 10, 15, 20 or 25 MHz)."));
        return;
    }
    m_savedHz = m_radioHz;
    m_savedMode = m_radioMode;
    m_carrierStage = 1;
    m_usbOk = false;
    m_carrierNote.clear();
    emit tuneForCarrier(m_nominalHz - 1000, false);
    m_carrierTimer.start(kSettleMs);
    setMessage(tr("Measuring the carrier: the radio goes to %1 kHz USB, then %2 kHz LSB, about %3 s in all, "
                  "then back.")
                   .arg((m_nominalHz - 1000) / 1000.0, 0, 'f', 3)
                   .arg((m_nominalHz + 1000) / 1000.0, 0, 'f', 3)
                   .arg(2 * (kSettleMs + kMeasureMs) / 1000));
    emit statsChanged();
}

void WwvEngine::checkRadioFollowed(qint64 dialHz, bool lsb)
{
    // What wfview reported while that sideband was measured.
    const bool modeOk
        = RadioModes::kind(m_radioMode) == RadioModes::Kind::Voice && RadioModes::isLsb(m_radioMode) == lsb;
    if (m_radioHz == dialHz && modeOk)
        return;
    if (!m_carrierNote.isEmpty())
        m_carrierNote += QStringLiteral("; ");
    m_carrierNote += tr("sent to %1 Hz %2, the radio reported %3 Hz %4")
                         .arg(dialHz)
                         .arg(lsb ? QStringLiteral("LSB") : QStringLiteral("USB"))
                         .arg(m_radioHz)
                         .arg(m_radioMode.toUpper());
}

void WwvEngine::cancelCarrier()
{
    if (m_carrierStage != 0)
        finishCarrier(false);
}

void WwvEngine::finishCarrier(bool ok)
{
    const bool measured = m_carrierStage == 4 && m_usbOk;
    m_carrierTimer.stop();
    m_carrierStage = 0;
    QMetaObject::invokeMethod(m_worker, "setCarrierMode", Qt::QueuedConnection, Q_ARG(bool, false));
    if (m_savedHz > 0)
        emit restoreRadio(m_savedHz, m_savedMode);
    if (ok && measured && m_snap.st.carrier && m_snap.st.carrierSeconds >= 4.0)
    {
        // A dial error moves the two tones opposite ways; the sound path (or a
        // sideband offset in the radio) moves both the same way.
        m_carrier.valid = true;
        m_carrier.nominalHz = m_nominalHz;
        m_carrier.usbHz = m_usbHz;
        m_carrier.lsbHz = m_snap.st.carrierHz - 1000.0;
        m_carrier.deltaHz = (m_carrier.usbHz - m_carrier.lsbHz) / 2.0;
        m_carrier.commonHz = (m_carrier.usbHz + m_carrier.lsbHz) / 2.0;
        m_carrier.seconds = m_usbSeconds + m_snap.st.carrierSeconds;
        m_carrier.note = m_carrierNote;
        m_carrier.when = QDateTime::currentDateTimeUtc();
        setMessage(tr("Carrier measured over %1 s.").arg(m_carrier.seconds, 0, 'f', 0));
    }
    else if (ok)
    {
        setMessage(tr("No steady carrier was found (signal too weak or fading?). Try again or another WWV "
                      "frequency."));
    }
    else
    {
        setMessage(tr("Carrier measurement stopped."));
    }
    if (!m_decoding)
        m_snap = WwvSnapshot();
    emit statsChanged();
}
