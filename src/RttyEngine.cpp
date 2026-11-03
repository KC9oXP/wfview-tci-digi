#include "RttyEngine.h"

#include "rttytxsource.h"

#include <QMetaType>
#include <QSettings>
#include <QThread>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <string>

namespace
{
constexpr int statsIntervalMs = 150;
constexpr double historySeconds = 6.0; // audio kept for auto-tune
constexpr double minTuneSeconds = 1.0;
constexpr int txPollMs = 40;
constexpr double prerollSeconds = 0.30; // mark before the first character (PTT settling)
constexpr double tailSeconds = 0.15; // mark after the last character
constexpr qint64 txStartTimeoutMs = 3000;
constexpr int scopeIntervalMs = 66; // ~15 frames per second
constexpr double scopePointsPerSecond = 4500.0;
constexpr size_t scopeMaxPoints = 400; // per frame

double levelDb(double level, double noise)
{
    return 20.0 * std::log10(std::max(level, 1e-12) / std::max(noise, 1e-12));
}

const char *const kDefaultMacroNames[RttyEngine::kMacroCount]
    = {"CQ", "Answer", "599", "TU 73", "QRZ?", "AGN?", "BTU", "RYRY"};
const char *const kDefaultMacroTexts[RttyEngine::kMacroCount] = {
    "CQ CQ CQ DE {MYCALL} {MYCALL} {MYCALL} PSE K\n",
    "{CALL} {CALL} DE {MYCALL} {MYCALL} {MYCALL} K\n",
    "{CALL} UR RST 599 599 DE {MYCALL} K\n",
    "{CALL} TU 73 DE {MYCALL} SK\n",
    "QRZ? DE {MYCALL} K\n",
    "AGN? AGN? K\n",
    "BTU {CALL} DE {MYCALL} K\n",
    "RYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRY\n",
};
} // namespace

// ---------------------------------------------------------------------------
// Worker (own QThread)
// ---------------------------------------------------------------------------

RttyDecoderWorker::RttyDecoderWorker(QObject *parent)
    : QObject(parent)
{
    m_cfg.sampleRate = 0.0; // set by the first audio block
    m_statsTimer.start();
    m_scopeTimer.start();
}

void RttyDecoderWorker::applyConfig()
{
    if (m_cfg.sampleRate <= 0.0)
        return;
    m_demod.setConfig(m_cfg);
    m_demod.setSquelch(m_squelch);
    m_scope.setConfig(m_cfg.sampleRate, m_cfg.markHz, m_cfg.spaceHz(), m_cfg.baud, m_cfg.shiftHz);
}

void RttyDecoderWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    if (m_cfg.sampleRate != sampleRate)
    {
        m_cfg.sampleRate = sampleRate;
        m_history.clear();
        applyConfig();
    }

    const std::string out = m_demod.process(mono.constData(), static_cast<size_t>(mono.size()));
    if (!out.empty())
        emit textDecoded(QString::fromLatin1(out.data(), static_cast<int>(out.size())));

    m_history.insert(m_history.end(), mono.constBegin(), mono.constEnd());
    const size_t keep = static_cast<size_t>(historySeconds * sampleRate);
    if (m_history.size() > 2 * keep)
        m_history.erase(m_history.begin(), m_history.end() - static_cast<std::ptrdiff_t>(keep));

    const size_t step = static_cast<size_t>(std::max(1.0, sampleRate / scopePointsPerSecond));
    m_scope.process(mono.constData(), static_cast<size_t>(mono.size()), step, m_scopePts);
    if (m_scopeTimer.elapsed() >= scopeIntervalMs)
    {
        m_scopeTimer.restart();
        const size_t first = m_scopePts.size() > scopeMaxPoints ? m_scopePts.size() - scopeMaxPoints : 0;
        QVector<float> xy;
        xy.reserve(static_cast<int>(2 * (m_scopePts.size() - first)));
        for (size_t i = first; i < m_scopePts.size(); ++i)
        {
            xy.append(m_scopePts[i].x);
            xy.append(m_scopePts[i].y);
        }
        m_scopePts.clear();
        emit scopeUpdated(xy);
    }

    if (m_statsTimer.elapsed() >= statsIntervalMs)
    {
        m_statsTimer.restart();
        const RttyDemodulator::Stats s = m_demod.stats();
        emit statsUpdated(s.quality,
                          levelDb(s.markLevel, s.noiseLevel),
                          levelDb(s.spaceLevel, s.noiseLevel),
                          s.squelchOpen,
                          m_cfg.sampleRate);
    }
}

void RttyDecoderWorker::setParams(
    double baud, double shiftHz, double markHz, bool reverse, double stopBits, bool usos, bool ita2, double squelch)
{
    m_cfg.baud = baud;
    m_cfg.shiftHz = shiftHz;
    m_cfg.markHz = markHz;
    m_cfg.reverse = reverse;
    m_cfg.stopBits = stopBits;
    m_cfg.usos = usos;
    m_cfg.ita2 = ita2;
    m_squelch = squelch;
    applyConfig();
}

void RttyDecoderWorker::resetDecoder()
{
    if (m_cfg.sampleRate > 0.0)
        m_demod.reset();
}

void RttyDecoderWorker::findTones(double shiftHz)
{
    const size_t keep = static_cast<size_t>(historySeconds * m_cfg.sampleRate);
    const size_t n = std::min(m_history.size(), keep);
    if (m_cfg.sampleRate <= 0.0 || n < static_cast<size_t>(minTuneSeconds * m_cfg.sampleRate))
    {
        emit tonesFound(false, 0.0, 0.0, 0.0);
        return;
    }
    const float *start = m_history.data() + (m_history.size() - n);
    const RttyTonePair p = rttyFindTonePair(start, n, m_cfg.sampleRate, shiftHz);
    emit tonesFound(p.found && p.levelDb >= 6.0, p.lowHz, p.highHz, p.levelDb);
}

// ---------------------------------------------------------------------------
// Controller (GUI thread)
// ---------------------------------------------------------------------------

RttyEngine::RttyEngine(QObject *parent)
    : QObject(parent)
    , m_tx(std::make_shared<RttyTxSource>())
{
    qRegisterMetaType<QVector<float>>("QVector<float>");

    for (int i = 0; i < kMacroCount; ++i)
    {
        m_macroNames << QString::fromLatin1(kDefaultMacroNames[i]);
        m_macroTexts << QString::fromLatin1(kDefaultMacroTexts[i]);
    }

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("rttyDecoder"));
    m_worker = new RttyDecoderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &RttyDecoderWorker::textDecoded, this, &RttyEngine::onTextDecoded);
    connect(m_worker, &RttyDecoderWorker::statsUpdated, this, &RttyEngine::onStatsUpdated);
    connect(m_worker, &RttyDecoderWorker::tonesFound, this, &RttyEngine::onTonesFound);
    connect(m_worker, &RttyDecoderWorker::scopeUpdated, this, &RttyEngine::onScopeUpdated);
    m_thread->start(QThread::LowPriority);

    m_txPoll.setInterval(txPollMs);
    connect(&m_txPoll, &QTimer::timeout, this, &RttyEngine::pollTx);

    m_status = tr("Off");
    applyConfig();
}

RttyEngine::~RttyEngine()
{
    m_tx->stop();
    m_thread->quit();
    m_thread->wait();
}

// ---- settings --------------------------------------------------------------

void RttyEngine::loadSettings(QSettings *settings)
{
    m_settings = settings;
    if (!m_settings)
        return;
    m_settings->beginGroup(QStringLiteral("RTTY"));
    m_baud = m_settings->value(QStringLiteral("Baud"), m_baud).toDouble();
    m_shift = m_settings->value(QStringLiteral("Shift"), m_shift).toInt();
    m_markHz = m_settings->value(QStringLiteral("Mark"), m_markHz).toInt();
    m_sideband = m_settings->value(QStringLiteral("Sideband"), m_sideband).toInt();
    m_reverse = m_settings->value(QStringLiteral("Reverse"), m_reverse).toBool();
    m_stopBits = m_settings->value(QStringLiteral("StopBits"), m_stopBits).toDouble();
    m_usos = m_settings->value(QStringLiteral("USOS"), m_usos).toBool();
    m_ita2 = m_settings->value(QStringLiteral("ITA2"), m_ita2).toBool();
    m_squelch = m_settings->value(QStringLiteral("Squelch"), m_squelch).toDouble();
    m_diddle = m_settings->value(QStringLiteral("Diddle"), m_diddle).toBool();
    m_txLevel = m_settings->value(QStringLiteral("TxLevel"), m_txLevel).toDouble();
    m_myCall = m_settings->value(QStringLiteral("MyCall"), m_myCall).toString();
    const QStringList names = m_settings->value(QStringLiteral("MacroNames")).toStringList();
    const QStringList texts = m_settings->value(QStringLiteral("MacroTexts")).toStringList();
    if (names.size() == kMacroCount && texts.size() == kMacroCount)
    {
        m_macroNames = names;
        m_macroTexts = texts;
    }
    m_settings->endGroup();

    m_baud = std::clamp(m_baud, 20.0, 300.0);
    m_shift = std::clamp(m_shift, 20, 1000);
    m_markHz = std::clamp(m_markHz, 200, 3500);
    m_sideband = std::clamp(m_sideband, 0, 2);
    m_squelch = std::clamp(m_squelch, 0.0, 0.95);
    m_txLevel = std::clamp(m_txLevel, 0.0, 1.0);
    applyConfig();
    emit settingsChanged();
    emit macrosChanged();
}

void RttyEngine::save(const char *key, const QVariant &value)
{
    if (!m_settings)
        return;
    m_settings->beginGroup(QStringLiteral("RTTY"));
    m_settings->setValue(QString::fromLatin1(key), value);
    m_settings->endGroup();
}

void RttyEngine::saveMacros()
{
    save("MacroNames", m_macroNames);
    save("MacroTexts", m_macroTexts);
}

bool RttyEngine::usbEffective() const
{
    if (m_sideband == SidebandUsb)
        return true;
    if (m_sideband == SidebandLsb)
        return false;
    // Auto: LSB and DIGL put the mark tone below space in the audio.
    const QString m = m_radioMode.toUpper();
    return !(m == QLatin1String("LSB") || m == QLatin1String("DIGL"));
}

RttyConfig RttyEngine::config() const
{
    RttyConfig c;
    c.baud = m_baud;
    c.shiftHz = m_shift;
    c.markHz = m_markHz;
    // USB: mark is the higher RF frequency and so the higher audio tone; the
    // space tone lies below it. LSB: the other way round. Reverse flips it.
    c.reverse = (usbEffective() != m_reverse);
    c.stopBits = m_stopBits;
    c.usos = m_usos;
    c.ita2 = m_ita2;
    return c;
}

int RttyEngine::spaceHz() const
{
    return static_cast<int>(std::lround(config().spaceHz()));
}

void RttyEngine::applyConfig()
{
    const RttyConfig c = config();
    QMetaObject::invokeMethod(m_worker,
                              "setParams",
                              Qt::QueuedConnection,
                              Q_ARG(double, c.baud),
                              Q_ARG(double, c.shiftHz),
                              Q_ARG(double, c.markHz),
                              Q_ARG(bool, c.reverse),
                              Q_ARG(double, c.stopBits),
                              Q_ARG(bool, c.usos),
                              Q_ARG(bool, c.ita2),
                              Q_ARG(double, m_squelch));
    m_tx->setConfig(c);
    m_tx->setLevel(static_cast<float>(m_txLevel));
    m_tx->setDiddle(m_diddle);
}

#define RTTY_SETTER(Name, member, key, Type)                                                                           \
    void RttyEngine::set##Name(Type v)                                                                                 \
    {                                                                                                                  \
        if (member == v)                                                                                               \
            return;                                                                                                    \
        member = v;                                                                                                    \
        save(key, member);                                                                                             \
        applyConfig();                                                                                                 \
        emit settingsChanged();                                                                                        \
    }

RTTY_SETTER(Reverse, m_reverse, "Reverse", bool)
RTTY_SETTER(Usos, m_usos, "USOS", bool)
RTTY_SETTER(Ita2, m_ita2, "ITA2", bool)
RTTY_SETTER(Diddle, m_diddle, "Diddle", bool)
#undef RTTY_SETTER

void RttyEngine::setSideband(int sb)
{
    sb = std::clamp(sb, 0, 2);
    if (sb == m_sideband)
        return;
    m_sideband = sb;
    save("Sideband", m_sideband);
    applyConfig();
    emit settingsChanged();
}

void RttyEngine::setRadioMode(const QString &mode)
{
    if (mode == m_radioMode)
        return;
    const bool before = usbEffective();
    m_radioMode = mode;
    if (usbEffective() != before)
    {
        applyConfig();
        resync();
    }
    emit settingsChanged();
}

void RttyEngine::setBaud(double baud)
{
    baud = std::clamp(baud, 20.0, 300.0);
    if (std::fabs(baud - m_baud) < 1e-6)
        return;
    m_baud = baud;
    save("Baud", m_baud);
    applyConfig();
    emit settingsChanged();
}

void RttyEngine::setShift(int hz)
{
    hz = std::clamp(hz, 20, 1000);
    if (hz == m_shift)
        return;
    m_shift = hz;
    save("Shift", m_shift);
    applyConfig();
    emit settingsChanged();
}

void RttyEngine::setMarkHz(int hz)
{
    hz = std::clamp(hz, 200, 3500);
    if (hz == m_markHz)
        return;
    m_markHz = hz;
    save("Mark", m_markHz);
    applyConfig();
    emit settingsChanged();
}

void RttyEngine::setStopBits(double bits)
{
    bits = bits < 1.25 ? 1.0 : (bits < 1.75 ? 1.5 : 2.0);
    if (bits == m_stopBits)
        return;
    m_stopBits = bits;
    save("StopBits", m_stopBits);
    applyConfig();
    emit settingsChanged();
}

void RttyEngine::setSquelch(double q)
{
    q = std::clamp(q, 0.0, 0.95);
    if (std::fabs(q - m_squelch) < 1e-6)
        return;
    m_squelch = q;
    save("Squelch", m_squelch);
    applyConfig();
    emit settingsChanged();
}

void RttyEngine::setTxLevel(double level)
{
    level = std::clamp(level, 0.0, 1.0);
    if (std::fabs(level - m_txLevel) < 1e-6)
        return;
    m_txLevel = level;
    save("TxLevel", m_txLevel);
    applyConfig();
    emit settingsChanged();
}

void RttyEngine::setMyCall(const QString &c)
{
    const QString v = c.trimmed().toUpper();
    if (v == m_myCall)
        return;
    m_myCall = v;
    save("MyCall", m_myCall);
    emit settingsChanged();
}

void RttyEngine::setCall(const QString &c)
{
    const QString v = c.trimmed().toUpper();
    if (v == m_call)
        return;
    m_call = v;
    emit settingsChanged();
}

void RttyEngine::setMacro(int index, const QString &name, const QString &text)
{
    if (index < 0 || index >= kMacroCount)
        return;
    m_macroNames[index] = name.trimmed().isEmpty() ? QStringLiteral("M%1").arg(index + 1) : name.trimmed();
    m_macroTexts[index] = text;
    saveMacros();
    emit macrosChanged();
}

// ---- receive ---------------------------------------------------------------

void RttyEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!m_decoding)
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void RttyEngine::setDecoding(bool on)
{
    if (m_decoding == on)
        return;
    m_decoding = on;
    if (on)
    {
        QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
        setStatus(m_ready ? tr("Listening...") : tr("Not connected - connect to wfview first."));
    }
    else
    {
        m_quality = m_markDb = m_spaceDb = 0.0;
        m_squelchOpen = false;
        emit statsChanged();
        setStatus(tr("Off"));
    }
    emit settingsChanged();
}

void RttyEngine::resync()
{
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
}

void RttyEngine::onTextDecoded(const QString &text)
{
    if (m_keyed || !m_decoding || text.isEmpty())
        return; // our own transmission is shown from the TX side
    emit textAppended(text);
    m_atLineStart = text.endsWith(QLatin1Char('\n'));
}

void RttyEngine::onStatsUpdated(double quality, double markDb, double spaceDb, bool squelchOpen, double sampleRate)
{
    if (!m_decoding)
        return;
    m_quality = quality;
    m_markDb = markDb;
    m_spaceDb = spaceDb;
    m_squelchOpen = squelchOpen;
    emit statsChanged();
    if (!m_keyed && m_status == tr("Listening..."))
        setStatus(tr("Decoding at %1 Hz audio").arg(static_cast<int>(sampleRate)));
}

void RttyEngine::autoTune()
{
    if (!m_decoding || !m_ready)
    {
        setStatus(tr("Auto-tune needs Decode on (it looks at the last few seconds of receive audio)."));
        return;
    }
    setStatus(tr("Looking for a %1 Hz tone pair...").arg(m_shift));
    QMetaObject::invokeMethod(m_worker, "findTones", Qt::QueuedConnection, Q_ARG(double, double(m_shift)));
}

void RttyEngine::onScopeUpdated(const QVector<float> &xy)
{
    if (m_decoding && !m_keyed)
        emit scopePoints(xy);
}

void RttyEngine::onTonesFound(bool found, double lowHz, double highHz, double levelDb)
{
    if (!found)
    {
        setStatus(
            tr("No RTTY tone pair %1 Hz apart found. Tune the signal into the passband and try again.").arg(m_shift));
        return;
    }
    // config().reverse == true means the space tone is below mark, i.e. mark is the higher tone.
    const double mark = config().reverse ? highHz : lowHz;
    setMarkHz(static_cast<int>(std::lround(mark)));
    resync();
    setStatus(tr("Tuned: mark %1 Hz, space %2 Hz (measured shift %3 Hz, %4 dB over noise)")
                  .arg(m_markHz)
                  .arg(spaceHz())
                  .arg(static_cast<int>(std::lround(highHz - lowHz)))
                  .arg(static_cast<int>(std::lround(levelDb))));
}

// ---- transmit --------------------------------------------------------------

void RttyEngine::setConnected(bool ready, bool canTransmit)
{
    m_ready = ready;
    m_canTransmit = ready && canTransmit;
    if (!m_canTransmit && m_keyed)
        release(ready ? tr("Transmit not available") : tr("Connection lost"));
    if (m_decoding && m_status != tr("Off"))
        setStatus(ready ? tr("Listening...") : tr("Not connected - connect to wfview first."));
}

void RttyEngine::setRadioTransmitting(bool on)
{
    m_radioTx = on;
    if (on && m_keyed)
        m_radioWasTx = true;
}

void RttyEngine::setHoldTx(bool on)
{
    if (m_holdTx == on)
        return;
    m_holdTx = on;
    if (!on && m_keyed)
        m_autoRelease = true;
    emit settingsChanged();
}

QString RttyEngine::expand(const QString &text) const
{
    QString t = text;
    t.replace(QStringLiteral("{MYCALL}"), m_myCall, Qt::CaseInsensitive);
    t.replace(QStringLiteral("{CALL}"), m_call, Qt::CaseInsensitive);
    return t.toUpper();
}

void RttyEngine::keyUp()
{
    if (m_keyed)
        return;
    if (!m_ready)
    {
        setStatus(tr("Not connected - connect to wfview first."));
        return;
    }
    if (!m_canTransmit)
    {
        setStatus(tr("The server reports this radio as receive only."));
        return;
    }
    m_tx->start(prerollSeconds);
    m_keyed = true;
    m_tailQueued = false;
    m_radioWasTx = false;
    m_keyedTimer.start();
    emit transmittingChanged(true);
    emit transmitRequested(true);
    m_txPoll.start();
    emit textAppended((m_atLineStart ? QString() : QStringLiteral("\n")) + QStringLiteral("[TX] "));
    m_atLineStart = false;
    setStatus(tr("Transmitting"));
}

void RttyEngine::release(const QString &why)
{
    if (!m_keyed)
        return;
    const std::string sent = m_tx->takeSent();
    m_tx->stop();
    m_txPoll.stop();
    m_keyed = false;
    m_autoRelease = false;
    emit transmitRequested(false);
    emit transmittingChanged(false);
    QString tail = QString::fromLatin1(sent.data(), static_cast<int>(sent.size()));
    if (!tail.endsWith(QLatin1Char('\n')))
        tail += QLatin1Char('\n');
    emit textAppended(tail);
    m_atLineStart = true;
    resync();
    setStatus(why.isEmpty() ? (m_decoding ? tr("Listening...") : tr("Off")) : why);
}

void RttyEngine::send(const QString &text)
{
    const QString t = expand(text);
    if (t.isEmpty())
        return;
    if (!m_keyed)
    {
        keyUp();
        if (!m_keyed)
            return;
        m_autoRelease = !m_holdTx;
    }
    m_tx->queueText(t.toLatin1().toStdString());
    m_tailQueued = false;
}

void RttyEngine::sendMacro(int index)
{
    if (index < 0 || index >= kMacroCount)
        return;
    const QString t = m_macroTexts.at(index);
    if (t.contains(QStringLiteral("{CALL}"), Qt::CaseInsensitive) && m_call.isEmpty())
    {
        setStatus(tr("Enter the other station's call first (the macro uses {CALL})."));
        return;
    }
    if (t.contains(QStringLiteral("{MYCALL}"), Qt::CaseInsensitive) && m_myCall.isEmpty())
    {
        setStatus(tr("Enter your own call in the My call field first (the macro uses {MYCALL})."));
        return;
    }
    send(t);
}

void RttyEngine::startTx()
{
    if (m_keyed)
    {
        m_autoRelease = false;
        return;
    }
    keyUp();
    m_autoRelease = false;
}

void RttyEngine::finishTx()
{
    if (m_keyed)
        m_autoRelease = true;
}

void RttyEngine::abortTx()
{
    release(tr("Transmit aborted"));
}

void RttyEngine::pollTx()
{
    if (!m_keyed)
    {
        m_txPoll.stop();
        return;
    }
    const std::string sent = m_tx->takeSent();
    if (!sent.empty())
        emit textAppended(QString::fromLatin1(sent.data(), static_cast<int>(sent.size())));

    const qint64 ms = m_keyedTimer.elapsed();
    if (ms > txStartTimeoutMs && m_tx->renderedFrames() == 0)
    {
        release(tr("No TX audio was requested by the server."));
        return;
    }
    if (ms > txStartTimeoutMs && !m_radioTx && !m_radioWasTx)
    {
        release(
            tr("The radio did not go to transmit (is Enable PTT Controls on in wfview's Settings > User Interface?)."));
        return;
    }
    if (m_radioWasTx && !m_radioTx)
    {
        release(tr("Transmit ended by the radio or wfview's PTT timeout."));
        return;
    }
    if (m_autoRelease && m_tx->idle())
    {
        if (!m_tailQueued)
        {
            m_tx->queueTail(tailSeconds);
            m_tailQueued = true;
        }
        else
        {
            release(QString());
        }
    }
}

void RttyEngine::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged(s);
}
