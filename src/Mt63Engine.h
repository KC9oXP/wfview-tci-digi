#pragma once

// MT63 receive and transmit (500 / 1000 / 2000 Hz, short or long interleave).
//
//   AudioHub::audio --pushAudio [queued]--> Mt63DecoderWorker (own QThread)
//       --textDecoded / statsUpdated [queued]--> Mt63Engine (GUI thread)
//
// The frequency is the centre of the signal. The receiver follows a few
// tens of Hz of offset by itself, so there is no AFC.

#include "ModemEngine.h"
#include "mt63core.h"

#include <QElapsedTimer>

class QThread;

class Mt63DecoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit Mt63DecoderWorker(QObject *parent = nullptr);

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setParams(int bandwidth, bool longInterleave, double centreHz, bool longIntegration, double squelchDb);
    void resetDecoder();

signals:
    void textDecoded(QString text);
    void statsUpdated(double snrDb, bool locked, double offsetHz, double squelchLevel, bool squelchOpen);

private:
    Mt63Demodulator m_demod;
    QElapsedTimer m_statsTimer;
};

class Mt63Engine : public ModemEngine
{
    Q_OBJECT

public:
    explicit Mt63Engine(QObject *parent = nullptr);
    ~Mt63Engine() override;

    void loadSettings(QSettings *settings) override;
    mt63::Config config() const;
    int bandwidth() const
    {
        return m_bandwidth;
    }
    bool longInterleave() const
    {
        return m_long;
    }
    // Squelch, dB (0 = off).
    double squelch() const
    {
        return m_squelch;
    }
    double squelchLevel() const
    {
        return m_squelchLevel;
    }
    bool squelchOpen() const
    {
        return m_squelchOpen;
    }
    double snrDb() const
    {
        return m_snrDb;
    }
    bool locked() const
    {
        return m_locked;
    }
    double offsetHz() const
    {
        return m_offset;
    }
    QString modeName() const;

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate) override;
    void setMode(int bandwidth, bool longInterleave);
    void setSquelch(double db);

protected:
    void applyConfig() override;
    void resetReceiver() override;
    QString levelKey() const override
    {
        return modeName();
    }
    // One interleave of zero characters before the text (as fldigi), so the
    // far end has the whole first block.
    double prerollSeconds() const override
    {
        return mt63::delaySeconds(config());
    }
    double tailSeconds() const override
    {
        return 0.0;
    }

private slots:
    void onStats(double snrDb, bool locked, double offsetHz, double squelchLevel, bool squelchOpen);

private:
    QThread *m_thread = nullptr;
    Mt63DecoderWorker *m_worker = nullptr;
    int m_bandwidth = 1000;
    bool m_long = true;
    double m_squelch = 5.5;
    double m_squelchLevel = 0.0;
    bool m_squelchOpen = false;
    double m_snrDb = -99.0;
    bool m_locked = false;
    double m_offset = 0.0;
};
