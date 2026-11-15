#pragma once

// WWV Time (receive only): time code, second ticks, tones and the carrier of
// WWV / WWVH, used to read UTC and to calibrate.
//
//   AudioHub::audio --pushAudio [queued, with its arrival time]--> WwvWorker
//       (own QThread) --snapshot [queued]--> WwvEngine (GUI thread)
//
// The computer's clock is compared with WWV by noting when each block of
// audio arrives: the earliest arrivals over a few seconds give the time of
// every sample, the decoder gives the sample at which a WWV second began.
// What is left after taking off the radio path (from the grid square) and
// the audio delay through radio and wfview (calibrated once, per radio) is
// the clock's error.

#include "wwvcore.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <deque>

class QSettings;
class QThread;

struct WwvSnapshot
{
    WwvDecoder::Status st;
    bool rawValid = false; // enough seconds compared with the PC clock
    double rawOffsetMs = 0.0; // PC time when a WWV second was heard minus that second
    double rawSpreadMs = 0.0;
    int rawCount = 0;
    qint64 utcTickMs = 0; // the WWV second of st.tickSample, ms since the epoch
};
Q_DECLARE_METATYPE(WwvSnapshot)

class WwvWorker : public QObject
{
    Q_OBJECT

public slots:
    void pushAudio(QVector<float> mono, float sampleRate, double arrivalEpochMs, bool live);
    void setCarrierMode(bool on);
    void resetDecoder();
    void clearTiming(); // after the PC clock was changed

signals:
    void snapshot(WwvSnapshot s);

private:
    WwvDecoder m_dec;
    double m_fs = 0.0;
    std::deque<std::pair<qint64, double>> m_arrivals; // arrival ms, arrival - sample time
    std::deque<double> m_raw;
    double m_lastTick = -1.0;
    QElapsedTimer m_emitTimer;
    int m_emitKey = -1;
};

class WwvEngine : public QObject
{
    Q_OBJECT

public:
    struct CarrierResult
    {
        bool valid = false;
        qint64 nominalHz = 0;
        double deltaHz = 0.0; // WWV reads this far above (+) the nominal on this radio
        double seconds = 0.0;
        QDateTime when;
    };

    explicit WwvEngine(QSettings *settings, QObject *parent = nullptr);
    ~WwvEngine() override;

    bool decoding() const
    {
        return m_decoding;
    }
    const WwvSnapshot &snap() const
    {
        return m_snap;
    }
    QString grid() const
    {
        return m_grid;
    }
    // ms, and the distance; false if the grid is not valid
    bool pathDelay(double &ms, double &km) const;
    double audioDelayMs() const
    {
        return m_audioDelayMs;
    }
    bool live() const
    {
        return m_live;
    }
    // PC clock minus true time, ms (+ = the PC is fast). False if not known yet.
    bool pcOffsetMs(double &ms) const;
    bool measuringCarrier() const
    {
        return m_carrierStage != 0;
    }
    int carrierSecondsLeft() const;
    CarrierResult carrierResult() const
    {
        return m_carrier;
    }
    QString message() const
    {
        return m_message;
    }
    // The standard frequency (2.5 ... 25 MHz) within 6 kHz of hz, or 0.
    static qint64 nearestStandard(qint64 hz);

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate);
    void setDecoding(bool on);
    void setGrid(const QString &grid);
    void setAudioDelayMs(double ms);
    void setLive(bool live); // false while a WAV is played into the decoders
    void setRadio(bool ready, qint64 hz, const QString &mode, const QString &device);

    // Takes the PC clock as right and sets the audio delay so the offset reads 0.
    void calibrateDelay();
    // Shifts the PC clock onto WWV (asks Windows for administrator rights).
    void setPcClock();
    void measureCarrier();
    void cancelCarrier();

signals:
    void settingsChanged();
    void statsChanged();
    void messageChanged(QString text);
    // Carrier measurement: tune 1 kHz below nominalHz in USB / put things back.
    void tuneForCarrier(qint64 dialHz);
    void restoreRadio(qint64 hz, QString mode);

private:
    void setMessage(const QString &m);
    void finishCarrier(bool ok);
    QString delayKey() const;

    QSettings *m_settings;
    QThread *m_thread = nullptr;
    WwvWorker *m_worker = nullptr;
    bool m_decoding = false;
    bool m_live = true;
    WwvSnapshot m_snap;
    QString m_grid;
    double m_audioDelayMs = 0.0;
    QString m_message;

    bool m_ready = false;
    qint64 m_radioHz = 0;
    QString m_radioMode, m_device;

    int m_carrierStage = 0; // 0 idle, 1 settling, 2 measuring
    qint64 m_savedHz = 0;
    QString m_savedMode;
    qint64 m_nominalHz = 0;
    QTimer m_carrierTimer;
    qint64 m_carrierEndMs = 0;
    CarrierResult m_carrier;
};
