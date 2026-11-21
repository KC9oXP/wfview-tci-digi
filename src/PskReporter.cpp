#include "PskReporter.h"

#include "memories.h"

#include <QHostInfo>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace
{
constexpr qint64 kIntervalMs = qint64(pskreporter::kSendIntervalSec) * 1000;
constexpr qint64 kTemplatesEveryMs = 3600 * 1000;
constexpr qint64 kResolveEveryMs = 3600 * 1000;
} // namespace

PskReporter::PskReporter(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_settings->beginGroup(QStringLiteral("PskReporter"));
    m_enabled = m_settings->value(QStringLiteral("Enabled"), false).toBool(); // off until turned on
    m_auto = m_settings->value(QStringLiteral("AutoDetect"), false).toBool();
    m_test = m_settings->value(QStringLiteral("TestServer"), false).toBool();
    m_antenna = m_settings->value(QStringLiteral("Antenna")).toString();
    m_settings->endGroup();
    m_myCall = m_settings->value(QStringLiteral("Station/MyCall")).toString().trimmed().toUpper();
    m_myGrid = m_settings->value(QStringLiteral("Station/MyGrid")).toString().trimmed();
    m_spotter.myCall = m_myCall.toStdString();
    m_randomId = QRandomGenerator::global()->generate();
    m_socket.bind(QHostAddress::AnyIPv4, 0);

    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &PskReporter::sendNow);
    if (m_enabled)
        schedule();
}

void PskReporter::save()
{
    m_settings->beginGroup(QStringLiteral("PskReporter"));
    m_settings->setValue(QStringLiteral("Enabled"), m_enabled);
    m_settings->setValue(QStringLiteral("AutoDetect"), m_auto);
    m_settings->setValue(QStringLiteral("TestServer"), m_test);
    m_settings->setValue(QStringLiteral("Antenna"), m_antenna);
    m_settings->endGroup();
}

bool PskReporter::validGrid(const QString &grid)
{
    static const QRegularExpression re(QStringLiteral("^[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2})?$"));
    return re.match(grid.trimmed()).hasMatch();
}

bool PskReporter::configured() const
{
    return callspot::isCallsign(m_myCall.toStdString()) && validGrid(m_myGrid);
}

QString PskReporter::modeFor(const QString &tab, const QString &subMode)
{
    const QString t = tab.toUpper();
    if (t == QLatin1String("CW"))
        return QStringLiteral("CW");
    if (t == QLatin1String("RTTY"))
        return QStringLiteral("RTTY");
    if (t == QLatin1String("PSK"))
    {
        QString s = subMode.toUpper(); // BPSK31 -> PSK31 (ADIF SUBMODE)
        if (s.startsWith(QLatin1Char('B')))
            s.remove(0, 1);
        return s.isEmpty() ? QStringLiteral("PSK") : s;
    }
    if (t == QLatin1String("OLIVIA"))
        return subMode.startsWith(QLatin1String("Contestia"), Qt::CaseInsensitive) ? QStringLiteral("CONTESTI")
                                                                                   : QStringLiteral("OLIVIA");
    if (t == QLatin1String("AMTOR"))
        return QStringLiteral("AMTORFEC");
    return QString(); // Hell (pictures of text), SSTV, NAVTEX, WEFAX, WWV: no callsigns to read
}

void PskReporter::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    save();
    if (on)
    {
        m_error.clear();
        schedule();
        emit message(
            tr("PSK Reporter on (%1, %2%3)").arg(m_myCall, m_myGrid, m_auto ? tr(", auto-detect") : QString()));
    }
    else
    {
        m_timer.stop();
        m_queue.clear();
        m_spotter.clear();
        emit message(tr("PSK Reporter off"));
    }
    emit statusChanged();
}

void PskReporter::setAutoDetect(bool on)
{
    m_auto = on;
    if (!on)
        m_spotter.clear();
    save();
    emit statusChanged();
}

void PskReporter::setTestServer(bool on)
{
    m_test = on;
    m_address.clear();
    save();
    emit statusChanged();
}

void PskReporter::setAntenna(const QString &a)
{
    m_antenna = a.trimmed();
    m_datagrams = 0; // the receiver template changes with it: send templates again
    save();
}

void PskReporter::setMyCall(const QString &call)
{
    const QString c = call.trimmed().toUpper();
    if (c == m_myCall)
        return;
    m_myCall = c;
    m_spotter.myCall = c.toStdString();
    emit statusChanged();
}

void PskReporter::setMyGrid(const QString &grid)
{
    const QString g = grid.trimmed();
    if (g == m_myGrid)
        return;
    m_myGrid = g;
    m_settings->setValue(QStringLiteral("Station/MyGrid"), g);
    emit statusChanged();
}

void PskReporter::feedText(const QString &tab, const QString &mode, const QString &text, qint64 rfHz)
{
    if (!m_enabled || !m_auto || mode.isEmpty() || rfHz <= 0)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const auto found = m_spotter.feed(tab.toStdString(), text.toLatin1().toStdString(), rfHz, now);
    for (const callspot::Spot &s : found)
        addSpot(QString::fromStdString(s.call), s.hz, mode);
}

void PskReporter::addSpot(const QString &call, qint64 rfHz, const QString &mode)
{
    if (!m_enabled || rfHz <= 0 || rfHz > 0xFFFFFFFFLL || mode.isEmpty())
        return;
    const QString c = call.trimmed().toUpper();
    if (!callspot::isCallsign(c.toStdString()) || c == m_myCall)
        return;
    pskreporter::Spot sp;
    sp.call = c.toStdString();
    sp.hz = quint32(rfHz);
    sp.mode = mode.toUpper().toStdString();
    sp.source = 1;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    sp.timeSec = quint32(now);
    std::string band = memories::bandName(rfHz);
    if (band.empty())
        band = std::to_string(rfHz / 1000000);
    if (m_queue.add(sp, now, band))
    {
        emit message(tr("PSK Reporter: %1 %2 %3 kHz queued").arg(c, mode).arg(rfHz / 1000.0, 0, 'f', 1));
        emit statusChanged();
    }
}

void PskReporter::schedule()
{
    // Every 5 minutes from when reporting was turned on, plus a few random seconds, so
    // stations do not all send on the clock.
    const qint64 jitter = QRandomGenerator::global()->bounded(20000);
    m_nextSendMs = QDateTime::currentMSecsSinceEpoch() + kIntervalMs + jitter;
    m_timer.start(int(kIntervalMs + jitter));
}

QDateTime PskReporter::nextSend() const
{
    return m_enabled && m_nextSendMs ? QDateTime::fromMSecsSinceEpoch(m_nextSendMs) : QDateTime();
}

void PskReporter::sendNow()
{
    if (!m_enabled)
        return;
    schedule();
    if (m_queue.waiting() == 0)
        return; // nothing heard: nothing to send
    if (!configured())
    {
        m_error = tr("needs your call sign and grid (Options > PSK Reporter...)");
        emit statusChanged();
        return;
    }
    QString host = QString::fromLatin1(m_test ? pskreporter::kTestHost : pskreporter::kHost);
    quint16 port = m_test ? pskreporter::kTestPort : pskreporter::kPort;
    // For the end-to-end test only (not in any dialog): send to a local port instead.
    const QString debugHost = m_settings->value(QStringLiteral("PskReporter/DebugHost")).toString();
    if (!debugHost.isEmpty())
    {
        const QHostAddress a(debugHost);
        const quint16 p = quint16(m_settings->value(QStringLiteral("PskReporter/DebugPort")).toUInt());
        if (!a.isNull() && p)
        {
            transmit(a, p);
            return;
        }
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_address.isNull() && nowMs - m_addressAtMs < kResolveEveryMs)
    {
        transmit(m_address, port);
        return;
    }
    QPointer<PskReporter> self(this);
    QHostInfo::lookupHost(host,
                          this,
                          [self, port, host](const QHostInfo &info)
                          {
                              if (!self)
                                  return;
                              QHostAddress addr;
                              for (const QHostAddress &a : info.addresses())
                                  if (a.protocol() == QAbstractSocket::IPv4Protocol)
                                  {
                                      addr = a;
                                      break;
                                  }
                              if (addr.isNull())
                              {
                                  self->m_error = tr("cannot find %1: %2").arg(host, info.errorString());
                                  emit self->statusChanged();
                                  return; // the spots wait for the next time
                              }
                              self->m_address = addr;
                              self->m_addressAtMs = QDateTime::currentMSecsSinceEpoch();
                              self->transmit(addr, port);
                          });
}

void PskReporter::transmit(const QHostAddress &to, quint16 port)
{
    pskreporter::Receiver r;
    r.call = m_myCall.toStdString();
    r.locator = m_myGrid.toStdString();
    r.software = std::string("wfview TCI Digi ") + APP_VERSION;
    r.antenna = m_antenna.toStdString();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    int sentNow = 0;
    // Normally one datagram; more only if one fills up (the page allows that).
    while (m_queue.waiting() > 0)
    {
        const bool templates = m_datagrams < 3 || nowMs - m_templatesAtMs > kTemplatesEveryMs;
        const size_t overhead = pskreporter::datagram(r, {}, templates, 0, 0, 0).size();
        const std::vector<pskreporter::Spot> spots = m_queue.take(overhead);
        const std::vector<std::uint8_t> d
            = pskreporter::datagram(r, spots, templates, quint32(nowMs / 1000), m_sequence, m_randomId);
        const qint64 n = m_socket.writeDatagram(reinterpret_cast<const char *>(d.data()), qint64(d.size()), to, port);
        if (n != qint64(d.size()))
        {
            m_error = tr("send failed: %1").arg(m_socket.errorString());
            emit message(tr("PSK Reporter: %1").arg(m_error));
            break;
        }
        m_error.clear();
        m_sequence += quint32(spots.size() + 1); // records: the senders and the receiver
        ++m_datagrams;
        if (templates)
            m_templatesAtMs = nowMs;
        m_sent += int(spots.size());
        sentNow += int(spots.size());
    }
    if (sentNow)
        emit message(tr("PSK Reporter: sent %n spot(s) to %1", nullptr, sentNow).arg(to.toString()));
    emit statusChanged();
}

QString PskReporter::statusText() const
{
    if (!m_enabled)
        return tr("PSK Reporter off. Click to turn on.");
    QStringList l;
    l << tr("PSK Reporter on as %1 (%2)%3").arg(m_myCall, m_myGrid, m_test ? tr(", TEST server") : QString());
    l << (m_auto ? tr("Reports: logged QSOs and calls seen after CQ, DE or 73.")
                 : tr("Reports: logged QSOs (auto-detect is off)."));
    l << tr("Sent %1, waiting %2.").arg(m_sent).arg(m_queue.waiting());
    const QDateTime next = nextSend();
    if (next.isValid())
        l << tr("Next send %1.").arg(next.toString(QStringLiteral("HH:mm:ss")));
    if (!m_error.isEmpty())
        l << tr("Problem: %1").arg(m_error);
    l << tr("Click to turn off.");
    return l.join(QLatin1Char('\n'));
}
