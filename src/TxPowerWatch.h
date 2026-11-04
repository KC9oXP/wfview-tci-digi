#pragma once

// Watches the radio's output power (TCI tx_sensors) while this program
// transmits. If the radio keys up but reports no power, the transmit audio is
// almost always going to a modulation input the radio is not using in its
// current data mode (e.g. wfview on LAN with the radio's data mode set to
// take USB audio). noPower() is emitted once per transmission so the window
// can say so. Servers that send no tx_sensors never trigger it.

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class TxPowerWatch : public QObject
{
    Q_OBJECT

public:
    explicit TxPowerWatch(QObject *parent = nullptr);

    // Below this the radio counts as "no output" (W, or % on servers that report percent).
    static constexpr double kNoPower = 0.5;
    static constexpr int kCheckAfterMs = 2500;
    static constexpr int kMinReports = 3;

    double maxPower() const
    {
        return m_maxPower;
    }
    int reports() const
    {
        return m_reports;
    }

public slots:
    void setOurTransmit(bool on); // this program asked for TX
    void setRadioTransmitting(bool on); // the server reports TX
    void onSensors(double alc, double power, double swr);

signals:
    // While transmitting, e.g. "Po 48.0  ALC 0.2  SWR 1.1"; empty when back on receive.
    void readout(QString text);
    void noPower();

private:
    void check();

    bool m_requested = false; // this program is asking for TX now
    bool m_ours = false; // the current transmission is this program's
    bool m_radioTx = false;
    QElapsedTimer m_since;
    QTimer m_checkTimer;
    int m_reports = 0;
    double m_maxPower = 0.0;
    bool m_warned = false;
};
