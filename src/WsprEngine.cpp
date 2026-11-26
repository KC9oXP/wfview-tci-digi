#include "WsprEngine.h"

#include "AppPaths.h"
#include "RadioModes.h"
#include "wsprtxsource.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMetaType>
#include <QRandomGenerator>
#include <QSettings>
#include <QTextStream>
#include <QThread>
#include <QTimeZone>
#include <algorithm>
#include <cmath>

namespace
{
constexpr qint64 kSlotMs = 120000;
constexpr double kDecodeAtSec = wspr::kRecordSec - wspr::kRecordLeadSec + 0.5; // 1:54.5 recorded, + latency
constexpr double kPlanAtSec = 116.0;
constexpr double kLateTxSec = 4.0; // a transmission is not started later than this into its slot
constexpr double kMaxMissingSec = 20.0;

QString hashFile()
{
    return QDir(AppPaths::base()).filePath(QStringLiteral("wspr-hashes.txt"));
}
QString spotsFile()
{
    return QDir(AppPaths::logs()).filePath(QStringLiteral("wspr-spots.txt"));
}
QString utcHm(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc()).toString(QStringLiteral("HH:mm"));
}
} // namespace

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------

void WsprWorker::reset()
{
    m_ring.clear();
    m_count = 0;
    m_t0.clear();
    m_lastArrival = 0.0;
    m_fs = 0.0;
}

void WsprWorker::pushAudio(QVector<float> mono, float sampleRate, double arrivalMs)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    const size_t ringSize = size_t(kHistorySec * wspr::kAudioRate);
    // A new rate or a gap in the audio: start the history again (sample times would be wrong).
    if (double(sampleRate) != m_fs || (m_lastArrival > 0.0 && arrivalMs - m_lastArrival > 2000.0))
    {
        reset();
        m_fs = sampleRate;
        m_rs.setRates(m_fs, wspr::kAudioRate);
        m_rs.reset();
    }
    if (m_ring.size() != ringSize)
        m_ring.assign(ringSize, 0.0f);
    m_lastArrival = arrivalMs;
    std::vector<float> out;
    m_rs.process(mono.constData(), size_t(mono.size()), out);
    for (float v : out)
        m_ring[size_t(m_count++ % qint64(ringSize))] = v;
    // Sample 0's time: the earliest arrival over the last 30 s (least delayed by the network).
    const double est = arrivalMs - double(m_count) * 1000.0 / wspr::kAudioRate;
    m_t0.emplace_back(arrivalMs, est);
    while (!m_t0.empty() && arrivalMs - m_t0.front().first > 30000.0)
        m_t0.pop_front();
}

void WsprWorker::loadHashes(QString text)
{
    m_dec.loadHashes(text.toStdString());
    m_knownCalls = m_dec.knownCalls();
}

void WsprWorker::decodeSlot(qint64 slotMs, qint64 dialHz, int passes)
{
    WsprSlot s;
    s.slotMs = slotMs;
    s.dialHz = dialHz;
    if (m_t0.empty() || m_ring.empty())
    {
        s.missingSec = wspr::kRecordSec;
        emit slotDone(s);
        return;
    }
    double t0 = m_t0.front().second;
    for (const auto &p : m_t0)
        t0 = std::min(t0, p.second);
    const qint64 ringSize = qint64(m_ring.size());
    const qint64 n = qint64(wspr::kRecordSec * wspr::kAudioRate);
    const qint64 first = qint64(std::llround((double(slotMs) - wspr::kRecordLeadSec * 1000.0 - t0) * wspr::kAudioRate / 1000.0));
    const qint64 oldest = std::max<qint64>(0, m_count - ringSize);
    std::vector<float> audio(size_t(n), 0.0f);
    qint64 missing = 0;
    for (qint64 k = 0; k < n; ++k)
    {
        const qint64 idx = first + k;
        if (idx < oldest || idx >= m_count)
            ++missing;
        else
            audio[size_t(k)] = m_ring[size_t(idx % ringSize)];
    }
    s.missingSec = double(missing) / wspr::kAudioRate;
    if (s.missingSec > kMaxMissingSec)
    {
        emit slotDone(s);
        return;
    }
    QElapsedTimer timer;
    timer.start();
    std::vector<float> i, q;
    wspr::toBaseband(audio, i, q);
    s.spots = m_dec.decode(i, q, dialHz, passes);
    s.spectrum = wspr::windowSpectrum(i, q);
    s.decodeSec = timer.elapsed() / 1000.0;
    s.valid = true;
    emit slotDone(s);
    const size_t known = m_dec.knownCalls();
    if (known != m_knownCalls)
    {
        m_knownCalls = known;
        emit hashesChanged(QString::fromStdString(m_dec.saveHashes()));
    }
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

WsprEngine::WsprEngine(QObject *parent)
    : ModemEngine(QStringLiteral("WSPR"), std::make_shared<WsprTxSource>(), parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    qRegisterMetaType<WsprSlot>("WsprSlot");
    m_carrierHz = wspr::kCentreHz;
    m_hop.rx.assign(wspr::bands().size(), false);
    m_hop.tx.assign(wspr::bands().size(), false);

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("wsprDecoder"));
    m_worker = new WsprWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &WsprWorker::slotDone, this, &WsprEngine::onSlotDone);
    connect(m_worker,
            &WsprWorker::hashesChanged,
            this,
            [](const QString &text)
            {
                QFile f(hashFile());
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                    f.write(text.toUtf8());
            });
    m_thread->setStackSize(4u * 1024u * 1024u); // headroom for the decoder (Windows threads default to 1 MB)
    m_thread->start(QThread::LowPriority);
    {
        QFile f(hashFile());
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            QMetaObject::invokeMethod(
                m_worker, "loadHashes", Qt::QueuedConnection, Q_ARG(QString, QString::fromUtf8(f.readAll())));
    }

    connect(this, &ModemEngine::transmittingChanged, this, &WsprEngine::onTransmitting);
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, &WsprEngine::tick);
    m_tick.start();
    m_slot = slotStart(QDateTime::currentMSecsSinceEpoch());
    m_slotState = tr("Waiting for the next even minute");
}

WsprEngine::~WsprEngine()
{
    m_thread->quit();
    m_thread->wait();
}

void WsprEngine::loadSettings(QSettings *s)
{
    if (s)
    {
        s->beginGroup(QStringLiteral("WSPR"));
        m_dbm = wspr::nearestDbm(s->value(QStringLiteral("Dbm"), 30).toInt());
        m_sched.setPercent(std::clamp(s->value(QStringLiteral("TxPercent"), 20).toInt(), 0, 100));
        m_txOffset = std::clamp(s->value(QStringLiteral("TxOffset"), 1500.0).toDouble(), 1410.0, 1590.0);
        m_autoFree = s->value(QStringLiteral("AutoFree"), true).toBool();
        m_passes = std::clamp(s->value(QStringLiteral("Passes"), 2).toInt(), 1, 3);
        m_autoPower = s->value(QStringLiteral("AutoPower"), false).toBool();
        m_feedLossDb = std::clamp(s->value(QStringLiteral("FeedLossDb"), 0.0).toDouble(), 0.0, 10.0);
        m_hop.mode = wspr::HopPlan::Mode(std::clamp(s->value(QStringLiteral("HopMode"), 0).toInt(), 0, 2));
        const QStringList rx = s->value(QStringLiteral("HopRx")).toStringList();
        const QStringList tx = s->value(QStringLiteral("HopTx")).toStringList();
        s->endGroup();
        for (size_t k = 0; k < wspr::bands().size(); ++k)
        {
            const QString name = QString::fromLatin1(wspr::bands()[k].name);
            m_hop.rx[k] = rx.contains(name);
            m_hop.tx[k] = tx.contains(name);
        }
        m_grid = s->value(QStringLiteral("Station/MyGrid")).toString().trimmed();
    }
    // Transmit is never on at start-up: the beacon is turned on by hand each time.
    m_txEnabled = false;
    ModemEngine::loadSettings(s);
}

QStringList WsprEngine::messages(QString *err) const
{
    std::string e;
    const std::vector<std::string> m = wspr::beaconMessages(myCall().toStdString(), m_grid.toStdString(), m_dbm, &e);
    if (myCall().isEmpty())
        e = "Enter your call (My call) first";
    if (err)
        *err = QString::fromStdString(e);
    QStringList out;
    if (!myCall().isEmpty())
        for (const std::string &s : m)
            out << QString::fromStdString(s);
    return out;
}

qint64 WsprEngine::slotStart(qint64 utcMs)
{
    return utcMs - ((utcMs % kSlotMs) + kSlotMs) % kSlotMs;
}

double WsprEngine::secondsIntoSlot() const
{
    return double(QDateTime::currentMSecsSinceEpoch() - m_slot) / 1000.0;
}

double WsprEngine::txProgress() const
{
    return std::static_pointer_cast<WsprTxSource>(txSource())->progress();
}

// ---- settings --------------------------------------------------------------

void WsprEngine::setGrid(const QString &grid)
{
    const QString g = grid.trimmed();
    if (g == m_grid)
        return;
    m_grid = g;
    if (settings() && (g.isEmpty() || wspr::validGrid(g.toStdString())))
        settings()->setValue(QStringLiteral("Station/MyGrid"), g);
    emit settingsChanged();
}

void WsprEngine::setDbm(int dbm)
{
    dbm = wspr::nearestDbm(dbm);
    if (dbm == m_dbm)
        return;
    m_dbm = dbm;
    save("Dbm", m_dbm);
    emit settingsChanged();
}

void WsprEngine::setTxEnabled(bool on)
{
    if (on == m_txEnabled)
        return;
    m_txEnabled = on;
    if (!on)
    {
        m_nextTx = false;
        m_forceNext = false;
        if (transmitting())
            abortTx();
    }
    else
        m_sched.reset();
    emit settingsChanged();
    emit slotChanged();
}

void WsprEngine::setTxPercent(int pct)
{
    pct = std::clamp(pct, 0, 100);
    if (pct == m_sched.percent())
        return;
    m_sched.setPercent(pct);
    save("TxPercent", pct);
    emit settingsChanged();
}

void WsprEngine::setTxOffsetHz(double hz)
{
    hz = std::clamp(std::round(hz), 1410.0, 1590.0);
    if (std::fabs(hz - m_txOffset) < 0.5)
        return;
    m_txOffset = hz;
    save("TxOffset", m_txOffset);
    emit settingsChanged();
    emit freeChanged();
}

void WsprEngine::setAutoFree(bool on)
{
    if (on == m_autoFree)
        return;
    m_autoFree = on;
    save("AutoFree", on);
    emit settingsChanged();
}

void WsprEngine::setPasses(int passes)
{
    passes = std::clamp(passes, 1, 3);
    if (passes == m_passes)
        return;
    m_passes = passes;
    save("Passes", passes);
    emit settingsChanged();
}

void WsprEngine::setAutoPower(bool on)
{
    if (on == m_autoPower)
        return;
    m_autoPower = on;
    save("AutoPower", on);
    m_powerNote = on ? tr("Power will follow the radio's reading after the next transmission.") : QString();
    emit settingsChanged();
}

void WsprEngine::setFeedLossDb(double db)
{
    db = std::clamp(std::round(db * 2.0) / 2.0, 0.0, 10.0);
    if (std::fabs(db - m_feedLossDb) < 0.01)
        return;
    m_feedLossDb = db;
    save("FeedLossDb", db);
    emit settingsChanged();
}

void WsprEngine::addTxPower(double watts)
{
    if (transmitting() && m_txClock.isValid())
        m_meter.add(watts, m_txClock.elapsed() / 1000.0);
}

void WsprEngine::onTransmitting(bool on)
{
    if (on)
    {
        m_meter.reset();
        m_txClock.start();
        return;
    }
    m_txClock.invalidate();
    if (!m_autoPower)
        return;
    double w = 0.0;
    std::string why;
    if (!m_meter.result(w, &why))
    {
        m_powerNote = tr("Power not changed: %1.").arg(QString::fromStdString(why));
        emit settingsChanged();
        return;
    }
    const int dbm = wspr::dbmForWatts(w, m_feedLossDb);
    m_powerNote = m_feedLossDb > 0.0
        ? tr("Measured %1 W at the radio, less %2 dB feed line: %3 dBm.").arg(w, 0, 'g', 3).arg(m_feedLossDb).arg(dbm)
        : tr("Measured %1 W at the radio: %2 dBm.").arg(w, 0, 'g', 3).arg(dbm);
    emit logLine(tr("[WSPR] %1\n").arg(m_powerNote));
    if (dbm != m_dbm)
    {
        m_dbm = dbm;
        save("Dbm", m_dbm);
    }
    emit settingsChanged();
}

void WsprEngine::setHopPlan(const wspr::HopPlan &plan)
{
    m_hop = plan;
    m_hop.rx.resize(wspr::bands().size(), false);
    m_hop.tx.resize(wspr::bands().size(), false);
    saveHop();
    emit settingsChanged();
}

void WsprEngine::saveHop()
{
    QStringList rx, tx;
    for (size_t k = 0; k < wspr::bands().size(); ++k)
    {
        if (m_hop.rx[k])
            rx << QString::fromLatin1(wspr::bands()[k].name);
        if (m_hop.tx[k])
            tx << QString::fromLatin1(wspr::bands()[k].name);
    }
    save("HopMode", int(m_hop.mode));
    save("HopRx", rx);
    save("HopTx", tx);
}

void WsprEngine::txNextSlot()
{
    m_forceNext = true;
    emit slotChanged();
    // Already planned (in the last seconds of a slot): turn the next one into a transmission now.
    if (m_planned && !m_nextTx && m_txEnabled)
    {
        m_nextTx = true;
        m_forceNext = false;
    }
}

void WsprEngine::setActive(bool shown)
{
    if (shown == m_active)
        return;
    m_active = shown;
    emit slotChanged();
}

void WsprEngine::setRadio(qint64 dialHz, const QString &mode, bool transmitting)
{
    const bool dialMoved = m_dial > 0 && dialHz != m_dial;
    m_mode = mode;
    m_radioTx = transmitting;
    if (dialMoved)
    {
        m_dial = dialHz;
        if (m_hopTarget > 0 && dialHz == m_hopTarget)
        {
            // Our own band hop, reported just after the slot began: record this slot on the new dial.
            m_hopTarget = 0;
            if (!m_decodeAsked && secondsIntoSlot() < 5.0)
                m_slotDial = dialHz;
        }
        else if (!m_decodeAsked)
            invalidate(tr("the dial moved"));
        emit freeChanged(); // another dial: its own busy list
    }
    else
        m_dial = dialHz;
    if (transmitting && !this->transmitting() && !m_decodeAsked)
        invalidate(tr("the radio transmitted"));
}

void WsprEngine::applyConfig()
{
    txSource()->setLevel(float(txLevel()));
}

void WsprEngine::resetReceiver() { }

void WsprEngine::onDecodingChanged(bool on)
{
    ModemEngine::onDecodingChanged(on);
    if (!on)
        invalidate(tr("Decode was turned off"));
    else
    {
        m_slotState = tr("Decoding starts with the slot at %1 UTC").arg(utcHm(m_slot + kSlotMs));
        setStatus(m_slotState);
        emit slotChanged();
    }
}

void WsprEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!decoding())
        return;
    QMetaObject::invokeMethod(m_worker,
                              "pushAudio",
                              Qt::QueuedConnection,
                              Q_ARG(QVector<float>, mono),
                              Q_ARG(float, sampleRate),
                              Q_ARG(double, double(QDateTime::currentMSecsSinceEpoch())));
}

// ---- slot clock ------------------------------------------------------------

void WsprEngine::invalidate(const QString &why)
{
    if (!m_slotRx)
        return;
    m_slotRx = false;
    m_slotState = tr("This slot is not decoded: %1").arg(why);
    emit slotChanged();
}

void WsprEngine::tick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 slot = slotStart(now);
    if (slot != m_slot)
        beginSlot(slot);
    const double sec = double(now - m_slot) / 1000.0;
    if (m_slotTx && !m_txStarted && sec >= wspr::kTxStartSec)
    {
        m_txStarted = true;
        if (sec <= kLateTxSec)
            startBeacon();
        else
        {
            m_slotTx = false;
            m_slotState = tr("Transmit slot missed (started %1 s late)").arg(qRound(sec));
            emit slotChanged();
        }
    }
    if (!m_decodeAsked && sec >= kDecodeAtSec)
    {
        m_decodeAsked = true;
        if (m_slotRx && decoding())
        {
            m_slotState = tr("Decoding the %1 slot...").arg(utcHm(m_slot));
            emit slotChanged();
            QMetaObject::invokeMethod(m_worker,
                                      "decodeSlot",
                                      Qt::QueuedConnection,
                                      Q_ARG(qint64, m_slot),
                                      Q_ARG(qint64, m_slotDial),
                                      Q_ARG(int, m_passes));
        }
    }
    if (!m_planned && sec >= kPlanAtSec)
    {
        m_planned = true;
        planNext(m_slot + kSlotMs);
    }
}

void WsprEngine::beginSlot(qint64 slot)
{
    const bool joinedLate = double(QDateTime::currentMSecsSinceEpoch() - slot) / 1000.0 > 2.0;
    m_slot = slot;
    m_txStarted = false;
    m_decodeAsked = false;
    m_planned = false;
    m_slotTx = m_nextTx && !joinedLate;
    m_nextTx = false;
    m_slotDial = m_dial;
    if (m_hopTarget > 0 && m_hopTarget == m_dial)
        m_hopTarget = 0;
    // The radio may still be on its way to the hop's dial (reported a moment later).
    const bool hopping = m_hopTarget > 0;
    m_slotRx = (m_dial > 0 || hopping) && decoding() && !m_slotTx && ready() && !m_radioTx && RadioModes::carriesAudio(m_mode);
    if (m_slotTx)
        m_slotState = tr("Transmit slot %1 UTC").arg(utcHm(slot));
    else if (m_slotRx)
        m_slotState = tr("Receiving the %1 UTC slot").arg(utcHm(slot));
    else if (!decoding())
        m_slotState = tr("Decode is off");
    else if (!ready())
        m_slotState = tr("Not connected");
    else if (!RadioModes::carriesAudio(m_mode))
        m_slotState = tr("The radio is not in a USB / data mode");
    else
        m_slotState = tr("Not decoding this slot");
    emit slotChanged();
}

void WsprEngine::planNext(qint64 nextSlot)
{
    const int minuteOfDay = int((nextSlot / 60000) % 1440);
    qint64 target = m_dial;
    const bool running = decoding() || m_txEnabled;
    if (running && m_active && m_hop.mode != wspr::HopPlan::Off && !transmitting() && ready())
    {
        const int b = m_hop.bandFor(minuteOfDay);
        if (b >= 0 && wspr::bands()[size_t(b)].dialHz != m_dial)
        {
            target = wspr::bands()[size_t(b)].dialHz;
            m_hopTarget = target;
            emit tuneRequested(target, tr("WSPR band hop to %1").arg(QLatin1String(wspr::bands()[size_t(b)].name)));
            emit logLine(tr("[WSPR] %1 UTC: hop to %2 (%3 MHz)\n")
                             .arg(utcHm(nextSlot), QLatin1String(wspr::bands()[size_t(b)].name))
                             .arg(double(target) / 1e6, 0, 'f', 6));
        }
    }
    m_nextTx = false;
    QString err;
    const bool canSend = m_txEnabled && m_active && canTransmit() && ready() && !messages(&err).isEmpty();
    bool bandOk = true;
    if (m_hop.mode != wspr::HopPlan::Off)
    {
        const int b = wspr::bandForDial(target);
        bandOk = b >= 0 && m_hop.tx[size_t(b)];
    }
    if (canSend && bandOk)
    {
        if (m_forceNext)
        {
            m_nextTx = true;
            m_forceNext = false;
        }
        else
            m_nextTx = m_sched.decide(nextSlot / kSlotMs, QRandomGenerator::global()->generateDouble());
    }
    emit slotChanged();
}

wspr::FreeFinder &WsprEngine::finder()
{
    return m_free[m_dial];
}

const wspr::FreeFinder *WsprEngine::finderFor(qint64 dial) const
{
    const auto it = m_free.find(dial);
    return it == m_free.end() ? nullptr : &it->second;
}

std::vector<wspr::FreeFinder::Busy> WsprEngine::busy() const
{
    const wspr::FreeFinder *f = finderFor(m_dial);
    return f ? f->busy(slotStart(QDateTime::currentMSecsSinceEpoch()) / kSlotMs) : std::vector<wspr::FreeFinder::Busy>();
}

std::vector<wspr::FreeFinder::Pick> WsprEngine::suggestions() const
{
    const wspr::FreeFinder *f = finderFor(m_dial);
    if (!f)
        return {};
    return f->suggest(slotStart(QDateTime::currentMSecsSinceEpoch()) / kSlotMs, 3);
}

double WsprEngine::clearance(double centreHz) const
{
    const wspr::FreeFinder *f = finderFor(m_dial);
    return f ? f->clearance(slotStart(QDateTime::currentMSecsSinceEpoch()) / kSlotMs, centreHz) : 1e9;
}

int WsprEngine::slotsHeard() const
{
    const wspr::FreeFinder *f = finderFor(m_dial);
    return f ? f->slotsSeen(slotStart(QDateTime::currentMSecsSinceEpoch()) / kSlotMs) : 0;
}

bool WsprEngine::clockHint(double &medianDt, int &count) const
{
    count = int(m_recentDt.size());
    if (count < 3)
        return false;
    std::vector<double> v(m_recentDt.begin(), m_recentDt.end());
    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2), v.end());
    medianDt = v[v.size() / 2];
    return true;
}

// ---- transmit --------------------------------------------------------------

void WsprEngine::startBeacon()
{
    QString err;
    const QStringList msgs = messages(&err);
    if (msgs.isEmpty())
    {
        m_slotState = tr("Not sent: %1").arg(err);
        setStatus(m_slotState);
        emit slotChanged();
        return;
    }
    const QString msg = msgs.at(m_msgIndex % msgs.size());
    ++m_msgIndex;
    std::array<std::uint8_t, wspr::kSymbols> sym;
    if (!wspr::encode(msg.toStdString(), sym))
    {
        m_slotState = tr("Not sent: \"%1\" could not be encoded").arg(msg);
        setStatus(m_slotState);
        emit slotChanged();
        return;
    }
    double centre = m_txOffset;
    QString moved;
    if (m_autoFree && slotsHeard() > 0 && clearance(centre) < 2.0)
    {
        const std::vector<wspr::FreeFinder::Pick> picks = suggestions();
        if (!picks.empty())
        {
            moved = tr(" (moved from %1 Hz, which is in use)").arg(qRound(centre));
            centre = picks.front().hz;
            m_txOffset = centre;
            save("TxOffset", m_txOffset);
            emit settingsChanged();
        }
    }
    std::static_pointer_cast<WsprTxSource>(txSource())->setMessage(sym, centre);
    startTx();
    if (!transmitting())
    {
        m_slotState = tr("Not sent: %1").arg(status());
        emit slotChanged();
        return;
    }
    finishTx(); // released when the 110.6 s message is out
    m_lastTxCentre = centre;
    const qint64 rf = m_dial + qint64(std::llround(centre));
    m_slotState = tr("Transmitting \"%1\" on %2 Hz%3").arg(msg).arg(qRound(centre)).arg(moved);
    setStatus(m_slotState);
    emit logLine(tr("[WSPR] %1 UTC TX \"%2\" at %3 MHz (audio %4 Hz)%5\n")
                     .arg(utcHm(m_slot), msg)
                     .arg(double(rf) / 1e6, 0, 'f', 6)
                     .arg(qRound(centre))
                     .arg(moved));
    emit slotChanged();
}

// ---- results ---------------------------------------------------------------

void WsprEngine::onSlotDone(WsprSlot s)
{
    if (!s.valid)
    {
        m_slotState = s.missingSec >= wspr::kRecordSec - 1.0
            ? tr("The %1 slot was not decoded (no audio)").arg(utcHm(s.slotMs))
            : tr("The %1 slot was not decoded (%2 s of audio missing)").arg(utcHm(s.slotMs)).arg(qRound(s.missingSec));
        setStatus(m_slotState);
        emit slotChanged();
        return;
    }
    m_free[s.dialHz].addSlot(s.slotMs / kSlotMs, s.spots, s.spectrum);
    if (s.dialHz == m_dial)
        m_lastSpectrum = s.spectrum;
    for (const wspr::Spot &p : s.spots)
    {
        m_recentDt.push_back(p.dt);
        while (m_recentDt.size() > 20)
            m_recentDt.pop_front();
    }
    // Spots log (like WSJT-X's ALL_WSPR.TXT): date time snr dt MHz message drift.
    if (!s.spots.empty())
    {
        QFile f(spotsFile());
        if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            QTextStream out(&f);
            const QString when
                = QDateTime::fromMSecsSinceEpoch(s.slotMs, QTimeZone::utc()).toString(QStringLiteral("yyMMdd HHmm"));
            for (const wspr::Spot &p : s.spots)
                out << QStringLiteral("%1 %2 %3 %4 %5 %6\n")
                           .arg(when)
                           .arg(qRound(p.snr), 3)
                           .arg(p.dt, 5, 'f', 1)
                           .arg(double(p.rfHz) / 1e6, 11, 'f', 6)
                           .arg(QString::fromStdString(p.message), -22)
                           .arg(qRound(p.drift), 2);
        }
    }
    m_slotState = s.spots.empty()
        ? tr("%1 UTC: nothing decoded (%2 s)").arg(utcHm(s.slotMs)).arg(s.decodeSec, 0, 'f', 1)
        : tr("%1 UTC: %n spot(s) (decoded in %2 s)", "", int(s.spots.size()))
              .arg(utcHm(s.slotMs))
              .arg(s.decodeSec, 0, 'f', 1);
    if (s.missingSec > 1.0)
        m_slotState += tr(", %1 s of audio missing").arg(qRound(s.missingSec));
    setStatus(m_slotState);
    emit slotDecoded(s);
    emit freeChanged();
    emit slotChanged();
}
