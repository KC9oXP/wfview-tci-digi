#pragma once

// PSK (BPSK31/63/125) receive and transmit.
//
//   TciClient::rxAudio --pushAudio [queued]--> PskDecoderWorker (own QThread)
//       --textDecoded / statsUpdated / centerFound / multiUpdated [queued]--> PskEngine (GUI thread)
//
// Multi-signal (optional): PskMultiDecoder in the same worker lists every PSK
// signal at the tab's speed, each copied on its own row.
//
// AFC moves the receive frequency; transmit uses the same frequency, like
// fldigi. Clicking the waterfall tunes there and snaps to the signal's centre.

#include "ModemEngine.h"
#include "SquelchSetting.h"
#include "pskcore.h"

#include <QElapsedTimer>
#include <QHash>
#include <memory>
#include <vector>

class QThread;
class PskMultiDecoder;

struct PskChannelInfo
{
    int id = 0;
    double hz = 0.0;
    double snrDb = 0.0;
    bool active = false;
    QString text;
    quint64 total = 0; // characters ever added to this channel's text
};
Q_DECLARE_METATYPE(PskChannelInfo)

class PskDecoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit PskDecoderWorker(QObject *parent = nullptr);
    ~PskDecoderWorker() override;

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setParams(double baud, double carrierHz, bool afc, double squelch, bool squelchDb, int generation);
    void resetDecoder();
    void findCenter(double approxHz);
    void detectSpeed(double approxHz);
    void setMulti(bool on);

signals:
    void textDecoded(QString text);
    void statsUpdated(
        double quality, double levelDb, double carrierHz, double squelchLevel, bool squelchOpen, int generation);
    void centerFound(double hz, double approxHz);
    void speedDetected(int speed, double approxHz, double s31, double s63, double s125);
    void rsqUpdated(PskDemodulator::Rsq rsq, int generation);
    void multiUpdated(QVector<PskChannelInfo> channels);

private:
    PskDemodulator m_demod;
    double m_fs = 0.0;
    double m_baud = 31.25;
    int m_generation = 0;
    std::vector<float> m_history;
    QElapsedTimer m_statsTimer;
    int m_rsqTick = 0;
    double m_squelch = 0.3;
    bool m_squelchDb = false;
    std::unique_ptr<PskMultiDecoder> m_multi;
    QElapsedTimer m_multiTimer;
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
    // Squelch on the scale in use: phase quality 0..0.95, or S/N 0..30 dB
    // with squelchInDb(). 0 = off.
    double squelch() const
    {
        return m_sq.value();
    }
    bool squelchInDb() const
    {
        return m_sq.inDb;
    }
    // What the squelch compares now (same scale).
    double squelchLevel() const
    {
        return m_squelchLevel;
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
    // List every PSK signal in the passband at this speed (default off).
    bool multiSignal() const
    {
        return m_multi;
    }

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate) override;
    void setSpeed(int speed);
    void setAfc(bool on);
    void setSquelch(double q);
    void setSquelchInDb(bool on);
    void setAutoSpeed(bool on);
    // Waterfall click: tune there, then snap to the signal centre (and set the
    // speed, with Auto speed on).
    void tuneTo(double hz);
    // Works out BPSK31/63/125 from the last 5 s of audio at the receive frequency.
    void detectSpeed();
    void setMultiSignal(bool on);
    // Decode the station at hz (from the multi-signal list); history is what the
    // list copied from it so far, put in the receive pane first.
    void decodeChannel(double hz, const QString &history = QString());

signals:
    void channelsChanged(QVector<PskChannelInfo> channels);
    // New text on one multi-signal row (call sign spotting).
    void channelText(int id, double hz, QString text);

protected:
    void applyConfig() override;
    void resetReceiver() override;
    void onDecodingChanged(bool on) override;
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
    void onStats(
        double quality, double levelDb, double carrierHz, double squelchLevel, bool squelchOpen, int generation);
    void onCenter(double hz, double approxHz);
    void onSpeed(int speed, double approxHz, double s31, double s63, double s125);
    void onMulti(const QVector<PskChannelInfo> &channels);

private:
    QThread *m_thread = nullptr;
    PskDecoderWorker *m_worker = nullptr;
    int m_speed = psk::Bpsk31;
    bool m_afc = true;
    SquelchSetting m_sq {0.30, 3.0, false, 0.95, 30.0};
    double m_squelchLevel = 0.0;
    bool m_autoSpeed = true;
    bool m_speedAsked = false; // a Detect speed press is waiting for its answer
    double m_quality = 0.0;
    double m_levelDb = -120.0;
    bool m_squelchOpen = false;
    int m_generation = 0;
    PskDemodulator::Rsq m_rsq;
    QElapsedTimer m_rsqTimer;
    bool m_multi = false;
    QHash<int, quint64> m_channelSeen; // per row id: characters already passed on
    double m_pickedHz = 0.0; // last station picked from the list, and when
    QElapsedTimer m_picked;
};

Q_DECLARE_METATYPE(PskDemodulator::Rsq)
