#include "TxPowerWatch.h"

#include <algorithm>

TxPowerWatch::TxPowerWatch(QObject *parent)
    : QObject(parent)
{
    m_checkTimer.setSingleShot(true);
    m_checkTimer.setInterval(kCheckAfterMs);
    connect(&m_checkTimer, &QTimer::timeout, this, &TxPowerWatch::check);
}

void TxPowerWatch::setOurTransmit(bool on)
{
    // A transmission counts as ours if we were asking for TX when the radio
    // keyed, or asked while it was keyed. The radio's "back to receive" from
    // an earlier transmission can arrive after a new request, so the request
    // is kept separately and only latched when the radio keys.
    m_requested = on;
    if (on && m_radioTx)
        m_ours = true;
}

void TxPowerWatch::setRadioTransmitting(bool on)
{
    if (on == m_radioTx)
        return;
    m_radioTx = on;
    if (on)
    {
        m_ours = m_requested;
        m_reports = 0;
        m_maxPower = 0.0;
        m_warned = false;
        m_since.start();
        m_checkTimer.start();
    }
    else
    {
        m_checkTimer.stop();
        if (m_since.isValid() && m_since.elapsed() >= kCheckAfterMs)
            check();
        m_ours = false;
        emit readout(QString());
    }
}

void TxPowerWatch::onSensors(double alc, double power, double swr)
{
    if (!m_radioTx)
        return;
    ++m_reports;
    m_maxPower = std::max(m_maxPower, power);
    emit readout(tr("Po %1  ALC %2  SWR %3").arg(power, 0, 'f', 1).arg(alc, 0, 'f', 1).arg(swr, 0, 'f', 1));
}

void TxPowerWatch::check()
{
    if (m_warned || !m_ours || m_reports < kMinReports || m_maxPower >= kNoPower)
        return;
    m_warned = true;
    emit noPower();
}
