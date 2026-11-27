#pragma once

// Olivia and Contestia receive and transmit (one tab, the mode picks which).
//
//   AudioHub::audio --pushAudio [queued]--> OliviaDecoderWorker (own QThread)
//       --textDecoded / statsUpdated [queued]--> OliviaEngine (GUI thread)
//
// The receiver searches +/- the tune margin around the set frequency by
// itself, so there is no AFC; the frequency is the centre of the signal.

#include "ModemEngine.h"
#include "SquelchSetting.h"
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
    void setParams(bool contestia, int tones, int bandwidth, double centreHz, double squelch, bool squelchDb);
    void resetDecoder();

signals:
    void textDecoded(QString text);
    void statsUpdated(double snr, double offsetHz, double squelchLevel, bool squelchOpen);

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
    // Squelch on the scale in use: the decoder's block S/N 0..20, or 0..30 dB
    // with squelchInDb(). 0 = off.
    double squelch() const
    {
        return m_sq.value();
    }
    bool squelchInDb() const
    {
        return m_sq.inDb;
    }
    double squelchLevel() const
    {
        return m_squelchLevel;
    }
    bool squelchOpen() const
    {
        return m_squelchOpen;
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
    void setSquelchInDb(bool on);

protected:
    void applyConfig() override;
    void resetReceiver() override;
    QString prepareText(const QString &expanded) const override
    {
        return m_contestia ? expanded.toUpper() : expanded;
    }
    QString levelKey() const override
    {
        return modeName();
    }
    // Zero characters before the text, so the far end has found the signal
    // before the first letter (fldigi and the app both lost it with none).
    double prerollSeconds() const override
    {
        return olivia::prerollSeconds(config());
    }
    double tailSeconds() const override
    {
        return 0.0;
    }

private slots:
    void onStats(double snr, double offsetHz, double squelchLevel, bool squelchOpen);

private:
    QThread *m_thread = nullptr;
    OliviaDecoderWorker *m_worker = nullptr;
    bool m_contestia = false;
    int m_tones = 8;
    int m_bandwidth = 500;
    SquelchSetting m_sq {6.0, 15.5, false, 20.0, 30.0};
    double m_squelchLevel = 0.0;
    bool m_squelchOpen = false;
    double m_snr = 0.0;
    double m_offset = 0.0;
};
