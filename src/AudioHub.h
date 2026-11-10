#pragma once

// Every decoder listens to AudioHub::audio, not to the TCI client directly:
//
//   TciClient::rxAudio --> AudioHub::fromRadio --> audio --> decoders, waterfall
//   WAV file (playback) ------------------------^  (radio audio is held off meanwhile)
//
// It also keeps the last few minutes of receive audio (save them as a WAV
// when you missed the start of something) and can record to a WAV file.

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>
#include <memory>
#include <vector>

class WavWriter;

class AudioHub : public QObject
{
    Q_OBJECT

public:
    explicit AudioHub(QObject *parent = nullptr);
    ~AudioHub() override;

    int ringSeconds() const
    {
        return m_ringSec;
    }
    void setRingSeconds(int sec);
    double bufferedSeconds() const;

    // Writes the last 'seconds' of receive audio to a WAV file in the
    // recordings folder; returns its path (empty on failure, err set).
    QString saveLast(int seconds, QString *err = nullptr);

    bool startRecording(QString *err = nullptr);
    void stopRecording();
    bool recording() const;
    QString recordingPath() const
    {
        return m_recPath;
    }

    // speed 1 = real time; higher replays faster (decoders keep up).
    bool play(const QString &path, double speed, QString *err = nullptr);
    void stopPlayback();
    bool playing() const
    {
        return m_playing;
    }
    QString playbackFile() const
    {
        return m_playPath;
    }

public slots:
    void fromRadio(const QVector<float> &mono, float sampleRate);

signals:
    void audio(QVector<float> mono, float sampleRate);
    void playbackChanged(bool playing, double progress); // progress 0..1
    void recordingChanged(bool recording, double seconds);

private slots:
    void playTick();

private:
    void toRing(const float *x, int n, float rate);

    int m_ringSec = 120;
    std::vector<float> m_ring;
    size_t m_ringPos = 0;
    size_t m_ringFill = 0;
    float m_ringRate = 0.0f;

    std::unique_ptr<WavWriter> m_rec;
    QString m_recPath;
    QElapsedTimer m_recNotify;

    bool m_playing = false;
    QString m_playPath;
    std::vector<float> m_play;
    double m_playRate = 48000.0;
    double m_playSpeed = 1.0;
    size_t m_playPos = 0;
    QTimer m_playTimer;
    QElapsedTimer m_playClock;
};
