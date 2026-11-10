#pragma once

// Olivia and Contestia receive and transmit (one tab, the mode picks which).
//
//   AudioHub::audio --pushAudio [queued]--> OliviaDecoderWorker (own QThread)
//       --textDecoded / statsUpdated [queued]--> OliviaEngine (GUI thread)
//
// The receiver searches +/- the tune margin around the set frequency by
// itself, so there is no AFC; the frequency is the centre of the signal.

#include "ModemEngine.h"
#include "oliviacore.h"

#include <QElapsedTimer>

class QThread;

class OliviaDecoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit OliviaDecoderWorker(QObject *parent = nullptr);

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setParams(bool contestia, int tones, int bandwidth, double centreHz, double squelch);
    void resetDecoder();

signals:
    void textDecoded(QString text);
    void statsUpdated(double snr, double offsetHz);

private:
    OliviaDemodulator m_demod;
    QElapsedTimer m_statsTimer;
};

class OliviaEngine : public ModemEngine
{
    Q_OBJECT

public:
    struct Preset
    {
        bool contestia;
        int tones;
        int bandwidth;
    };
    static const QVector<Preset> &presets();
    static QString presetName(const Preset &p);

    explicit OliviaEngine(QObject *parent = nullptr);
    ~OliviaEngine() override;

    void loadSettings(QSettings *settings) override;
    olivia::Config config() const;
    bool contestia() const
    {
        return m_contestia;
    }
    int tones() const
    {
        return m_tones;
    }
    int bandwidth() const
    {
        return m_bandwidth;
    }
    double squelch() const
    {
        return m_squelch;
    }
    double snr() const
    {
        return m_snr;
    }
    double offsetHz() const
    {
        return m_offset;
    }
    QString modeName() const;

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate) override;
    void setMode(bool contestia, int tones, int bandwidth);
    void setSquelch(double snr);

protected:
    void applyConfig() override;
    void resetReceiver() override;
    QString prepareText(const QString &expanded) const override
    {
        return m_contestia ? expanded.toUpper() : expanded;
    }
    double prerollSeconds() const override
    {
        return 0.0;
    }
    double tailSeconds() const override
    {
        return 0.0;
    }

private slots:
    void onStats(double snr, double offsetHz);

private:
    QThread *m_thread = nullptr;
    OliviaDecoderWorker *m_worker = nullptr;
    bool m_contestia = false;
    int m_tones = 8;
    int m_bandwidth = 500;
    double m_squelch = 5.0;
    double m_snr = 0.0;
    double m_offset = 0.0;
};
