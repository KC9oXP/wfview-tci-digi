#include "GridTrackerLink.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace
{
const std::string kId = "wfview-tci-digi";
}

GridTrackerLink::GridTrackerLink(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_settings->beginGroup(QStringLiteral("GridTracker"));
    m_enabled = m_settings->value(QStringLiteral("Enabled"), false).toBool();
    const QHostAddress h(m_settings->value(QStringLiteral("Host"), QStringLiteral("127.0.0.1")).toString());
    if (!h.isNull())
        m_host = h;
    m_port = quint16(m_settings->value(QStringLiteral("Port"), 2237).toUInt());
    if (m_port == 0)
        m_port = 2237;
    m_settings->endGroup();

    // A call is reported once, then again only after a minute.
    m_spotter.needed = 1;
    m_spotter.windowSec = 60;
    m_spotter.maxSpreadHz = 1500;

    m_socket.bind(QHostAddress::AnyIPv4, 0);
    m_beat.setInterval(15000);
    connect(&m_beat,
            &QTimer::timeout,
            this,
            [this]()
            {
                sendHeartbeat();
                sendStatus();
            });
    m_soon.setSingleShot(true);
    m_soon.setInterval(200);
    connect(&m_soon, &QTimer::timeout, this, &GridTrackerLink::sendStatus);
    if (m_enabled)
    {
        m_beat.start();
        QTimer::singleShot(0,
                           this,
                           [this]()
                           {
                               sendHeartbeat();
                               sendStatus();
                           });
    }
}

void GridTrackerLink::save()
{
    m_settings->beginGroup(QStringLiteral("GridTracker"));
    m_settings->setValue(QStringLiteral("Enabled"), m_enabled);
    m_settings->setValue(QStringLiteral("Host"), m_host.toString());
    m_settings->setValue(QStringLiteral("Port"), m_port);
    m_settings->endGroup();
}

void GridTrackerLink::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    m_errorShown = false;
    m_spotter.clear();
    save();
    if (on)
    {
        m_beat.start();
        sendHeartbeat();
        sendStatus();
        emit message(tr("GridTracker: sending to %1:%2.").arg(m_host.toString()).arg(m_port));
    }
    else
    {
        m_beat.stop();
        m_soon.stop();
        emit message(tr("GridTracker: off."));
    }
    emit statusChanged();
}

bool GridTrackerLink::setTarget(const QString &host, quint16 port)
{
    const QHostAddress h(host.trimmed());
    if (h.isNull() || port == 0)
        return false;
    m_host = h;
    m_port = port;
    m_errorShown = false;
    save();
    if (m_enabled)
    {
        sendHeartbeat();
        sendStatus();
    }
    return true;
}

void GridTrackerLink::setDial(qint64 hz)
{
    const std::uint64_t v = hz > 0 ? std::uint64_t(hz) : 0;
    if (v == m_status.dialHz)
        return;
    m_status.dialHz = v;
    sendStatusSoon();
}

void GridTrackerLink::setMode(const QString &mode)
{
    const std::string m = mode.toUpper().toStdString();
    if (m == m_status.mode)
        return;
    m_status.mode = m;
    sendStatusSoon();
}

void GridTrackerLink::setMyCall(const QString &call)
{
    const std::string c = call.trimmed().toUpper().toStdString();
    m_spotter.myCall = c;
    if (c == m_status.deCall)
        return;
    m_status.deCall = c;
    sendStatusSoon();
}

void GridTrackerLink::setMyGrid(const QString &grid)
{
    // WSJT-X form ("EN71ar"); "" if not a valid locator, so GridTracker keeps its own
    const std::string g = wsjtx::normalGrid(grid.trimmed().toStdString());
    if (g == m_status.deGrid)
        return;
    m_status.deGrid = g;
    sendStatusSoon();
}

void GridTrackerLink::setTransmitting(bool on)
{
    if (on == m_status.transmitting)
        return;
    m_status.transmitting = on;
    m_status.decoding = !on;
    sendStatusSoon();
}

void GridTrackerLink::setRxDf(double audioHz)
{
    const std::uint32_t df = audioHz > 0 && audioHz < 10000 ? std::uint32_t(std::lround(audioHz)) : 0;
    if (df == m_status.rxDf)
        return;
    m_status.rxDf = df;
    m_status.txDf = df;
    sendStatusSoon();
}

void GridTrackerLink::feedText(
    const QString &tab, const QString &mode, const QString &text, double audioHz, qint64 rfHz, double snr)
{
    if (!m_enabled || mode.isEmpty() || rfHz <= 0)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const auto found = m_spotter.feed(tab.toStdString(), text.toLatin1().toStdString(), rfHz, now);
    if (found.empty())
        return;
    const QTime t = QDateTime::currentDateTimeUtc().time();
    for (const callspot::Spot &s : found)
    {
        wsjtx::DecodeInfo d;
        d.msSinceMidnightUtc = std::uint32_t(t.msecsSinceStartOfDay());
        d.snr = std::isnan(snr) ? 0 : std::int32_t(std::lround(snr));
        d.df = audioHz > 0 && audioHz < 10000 ? std::uint32_t(std::lround(audioHz)) : 0;
        d.mode = mode.toUpper().toStdString();
        d.message = wsjtx::messageFor(s.call, s.via, s.to);
        send(wsjtx::decode(kId, d));
        ++m_decodes;
        m_sentLines << QString::fromStdString(d.message);
        if (m_sentLines.size() > 200)
            m_sentLines.removeFirst();
    }
    emit statusChanged();
}

void GridTrackerLink::wsprSpot(qint64 slotUtcMs, int snr, double dt, qint64 rfHz, int drift, const QString &call,
                               const QString &grid, int dbm)
{
    if (!m_enabled || rfHz <= 0 || call.isEmpty() || call.startsWith(QLatin1Char('<')))
        return;
    wsjtx::WsprInfo d;
    d.msSinceMidnightUtc = std::uint32_t(((slotUtcMs % 86400000LL) + 86400000LL) % 86400000LL);
    d.snr = snr;
    d.dt = dt;
    d.rfHz = std::uint64_t(rfHz);
    d.drift = drift;
    d.call = call.toUpper().toStdString();
    d.grid = wsjtx::normalGrid(grid.toStdString());
    d.dbm = dbm;
    send(wsjtx::wsprDecode(kId, d));
    ++m_decodes;
    m_sentLines << QStringLiteral("WSPR %1 %2 %3").arg(call, grid).arg(dbm);
    if (m_sentLines.size() > 200)
        m_sentLines.removeFirst();
    emit statusChanged();
}

void GridTrackerLink::sendStatusSoon()
{
    if (m_enabled && !m_soon.isActive())
        m_soon.start();
}

void GridTrackerLink::sendStatus()
{
    if (!m_enabled)
        return;
    wsjtx::StatusInfo s = m_status;
    if (!s.transmitting)
        s.decoding = true;
    send(wsjtx::status(kId, s));
}

void GridTrackerLink::sendHeartbeat()
{
    if (m_enabled)
        send(wsjtx::heartbeat(kId, APP_VERSION));
}

void GridTrackerLink::send(const std::vector<std::uint8_t> &bytes)
{
    const qint64 n = m_socket.writeDatagram(
        reinterpret_cast<const char *>(bytes.data()), qint64(bytes.size()), m_host, m_port);
    if (n < 0 && !m_errorShown)
    {
        m_errorShown = true; // once until settings change
        emit message(tr("GridTracker: could not send to %1:%2 (%3).")
                         .arg(m_host.toString())
                         .arg(m_port)
                         .arg(m_socket.errorString()));
    }
}

void GridTrackerLink::editSettings(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("GridTracker"));
    auto *v = new QVBoxLayout(&dlg);
    auto *about = new QLabel(
        tr("Sends GridTracker2 the dial frequency, band and mode, and the calls heard after CQ, DE or 73 "
           "(each call at most once a minute), so they show on its map.\n\n"
           "In GridTracker2: Settings > General > Receive UDP Messages, port 2237 (GridTracker's default; "
           "WSJT-X can keep sending there too). "
           "Logged QSOs still go to GridTracker through Options > Logging and uploads (ADIF, port 2333)."));
    about->setWordWrap(true);
    v->addWidget(about);
    auto *form = new QFormLayout();
    auto *enable = new QCheckBox(tr("Send to GridTracker"));
    enable->setChecked(m_enabled);
    form->addRow(enable);
    auto *host = new QLineEdit(m_host.toString());
    host->setToolTip(tr("IP address: 127.0.0.1 for this PC, a LAN address, or a multicast group (e.g. 224.0.0.1)."));
    form->addRow(tr("Address"), host);
    auto *port = new QSpinBox();
    port->setRange(1, 65535);
    port->setValue(m_port);
    form->addRow(tr("Port"), port);
    auto *count = new QLabel(tr("Calls sent since start: %1").arg(m_decodes));
    form->addRow(count);
    v->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (!setTarget(host->text(), quint16(port->value())))
        QMessageBox::warning(parent,
                             tr("GridTracker"),
                             tr("\"%1\" is not an IP address; the address was not changed.").arg(host->text()));
    setEnabled(enable->isChecked());
}
