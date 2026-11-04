#pragma once

// Software CW decoder: runs CwDecoderCore (and optionally CwMultiDecoder) on
// receive audio in its own thread.
//
//   TciClient::rxAudio --pushAudio [queued]--> CwDecoderWorker (own QThread)
//       --textDecoded / statsUpdated / multiUpdated [queued]--> CwDecoder (GUI thread)

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVector>
#include <memory>

class CwDecoderCore;
class CwMultiDecoder;
class QThread;

struct CwChannelInfo
{
    int id = 0;
    double pitchHz = 0.0;
    double wpm = 0.0;
    double snrDb = 0.0;
    bool active = false;
    QString text;
};
Q_DECLARE_METATYPE(CwChannelInfo)

class CwDecoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit CwDecoderWorker(QObject *parent = nullptr);
    ~CwDecoderWorker() override;

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setPitch(bool fixed, float fixedHz); // fixed=false: track 200-1200 Hz
    void setSpeedWpm(float wpm); // <= 0 = auto
    void setSquelch(bool on);
    void setEngine(int engine);
    void setMulti(bool on);
    void resetDecoder();

signals:
    void textDecoded(QString text);
    void multiUpdated(QVector<CwChannelInfo> channels);
    void statsUpdated(float pitchHz, float speedWpm, float snrDb, bool locked, bool squelchOpen, float sampleRate);

private:
    std::unique_ptr<CwDecoderCore> m_core;
    std::unique_ptr<CwMultiDecoder> m_multi;
    QElapsedTimer m_statsTimer;
    QElapsedTimer m_multiTimer;
};

class CwDecoder : public QObject
{
    Q_OBJECT

public:
    enum Engine
    {
        EngineMachine = 0, // ggmorse: keyer / computer-sent CW
        EngineHandKey = 1 // learns each sender's own timing
    };

    explicit CwDecoder(QObject *parent = nullptr);
    ~CwDecoder() override;

    static int pitchMin();
    static int pitchMax();
    static int machineMaxPitch();

    bool enabled() const
    {
        return m_enabled;
    }
    bool squelch() const
    {
        return m_squelch;
    }
    int engine() const
    {
        return m_engine;
    }
    bool multiSignal() const
    {
        return m_multi;
    }
    bool fixedPitch() const
    {
        return m_fixedPitch;
    }
    int pitchHz() const
    {
        return m_pitchHz;
    }
    // The pitch being decoded now: the fixed pitch, or the last tracked one.
    int currentPitchHz() const
    {
        return m_fixedPitch || m_lastPitchHz <= 0 ? m_pitchHz : m_lastPitchHz;
    }
    bool fixedSpeed() const
    {
        return m_fixedSpeed;
    }
    int speedWpm() const
    {
        return m_speedWpm;
    }

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate);
    void setEnabled(bool on);
    void setSquelch(bool on);
    void setEngine(int engine);
    void setMultiSignal(bool on);
    void setFixedPitch(bool on);
    void setPitchHz(int hz);
    void setFixedSpeed(bool on);
    void setSpeedWpm(int wpm);
    void resync();
    // Decode the station at this pitch (from the multi-signal list).
    void decodeChannel(int pitchHz);
    // Hold off decoding (our own transmission).
    void setPaused(bool on);

signals:
    void textAppended(QString text);
    void statusChanged(QString status);
    void channelsChanged(QVector<CwChannelInfo> channels);
    void settingsChanged();

private slots:
    void onStats(float pitchHz, float speedWpm, float snrDb, bool locked, bool squelchOpen, float sampleRate);
    void onMulti(const QVector<CwChannelInfo> &channels);
    void onText(const QString &text);

private:
    void apply();
    int effectiveEngine() const;

    QThread *m_thread = nullptr;
    CwDecoderWorker *m_worker = nullptr;
    bool m_enabled = false;
    bool m_paused = false;
    bool m_squelch = true;
    int m_engine = EngineMachine;
    int m_appliedEngine = -1;
    bool m_multi = false;
    bool m_fixedPitch = false;
    int m_pitchHz = 600;
    int m_lastPitchHz = 0;
    bool m_fixedSpeed = false;
    int m_speedWpm = 20;
};
