#pragma once

// PSK (BPSK31/63/125) receive and transmit.
//
//   TciClient::rxAudio --pushAudio [queued]--> PskDecoderWorker (own QThread)
//       --textDecoded / statsUpdated / centerFound [queued]--> PskEngine (GUI thread)
//
// AFC moves the receive frequency; transmit uses the same frequency, like
// fldigi. Clicking the waterfall tunes there and snaps to the signal's centre.

#include "ModemEngine.h"
#include "pskcore.h"

#include <QElapsedTimer>
#include <vector>

class QThread;

class PskDecoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit PskDecoderWorker(QObject *parent = nullptr);

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setParams(double baud, double carrierHz, bool afc, double squelch, int generation);
    void resetDecoder();
    void findCenter(double approxHz);
    void detectSpeed(double approxHz);

signals:
    void textDecoded(QString text);
    void statsUpdated(double quality, double levelDb, double carrierHz, bool squelchOpen, int generation);
    void centerFound(double hz, double approxHz);
    void speedDetected(int speed, double approxHz, double s31, double s63, double s125);
    void rsqUpdated(PskDemodulator::Rsq rsq, int generation);

private:
    PskDemodulator m_demod;
    double m_fs = 0.0;
    double m_baud = 31.25;
    int m_generation = 0;
    std::vector<float> m_history;
    QElapsedTimer m_statsTimer;
    int m_rsqTick = 0;
};

class PskEngine : public ModemEngine
{
    Q_OBJECT

public:
    explicit PskEngine(QObject *parent = nullptr);
    ~PskEngine() override;

    void loadSettings(QSettings *settings) override;

    int speed() const
    {
        return m_speed;
    }
    double baud() const
    {
        return psk::baudFor(m_speed);
    }
    bool afc() const
    {
        return m_afc;
    }
    double squelch() const
    {
        return m_squelch;
    }
    // Set the speed from the audio after each waterfall click (default on).
    bool autoSpeed() const
    {
        return m_autoSpeed;
    }
    double quality() const
    {
        return m_quality;
    }
    double levelDb() const
    {
        return m_levelDb;
    }
    bool squelchOpen() const
    {
        return m_squelchOpen;
    }
    // Measurements for an RSQ report of the signal being received (see psk::rsq*);
    // rsqAgeMs() is how long ago they were last updated (-1 never).
    PskDemodulator::Rsq rsq() const
    {
        return m_rsq;
    }
    qint64 rsqAgeMs() const
    {
        return m_rsqTimer.isValid() ? m_rsqTimer.elapsed() : -1;
    }

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate) override;
    void setSpeed(int speed);
    void setAfc(bool on);
    void setSquelch(double q);
    void setAutoSpeed(bool on);
    // Waterfall click: tune there, then snap to the signal centre (and set the
    // speed, with Auto speed on).
    void tuneTo(double hz);
    // Works out BPSK31/63/125 from the last 5 s of audio at the receive frequency.
    void detectSpeed();

protected:
    void applyConfig() override;
    void resetReceiver() override;
    QString levelKey() const override
    {
        return QString::fromLatin1(psk::speedName(m_speed));
    }
    double prerollSeconds() const override
    {
        return 32.0 / baud(); // 32 symbols of reversals
    }
    double tailSeconds() const override
    {
        return 8.0 / baud();
    }

private slots:
    void onStats(double quality, double levelDb, double carrierHz, bool squelchOpen, int generation);
    void onCenter(double hz, double approxHz);
    void onSpeed(int speed, double approxHz, double s31, double s63, double s125);

private:
    QThread *m_thread = nullptr;
    PskDecoderWorker *m_worker = nullptr;
    int m_speed = psk::Bpsk31;
    bool m_afc = true;
    double m_squelch = 0.3;
    bool m_autoSpeed = true;
    bool m_speedAsked = false; // a Detect speed press is waiting for its answer
    double m_quality = 0.0;
    double m_levelDb = -120.0;
    bool m_squelchOpen = false;
    int m_generation = 0;
    PskDemodulator::Rsq m_rsq;
    QElapsedTimer m_rsqTimer;
};

Q_DECLARE_METATYPE(PskDemodulator::Rsq)
