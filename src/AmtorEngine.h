#pragma once

// AMTOR FEC (mode B, as SITOR-B) receive and transmit.
//
//   AudioHub::audio --pushAudio [queued]--> NavtexWorker (own QThread; the
//       SITOR-B receiver the NAVTEX tab uses) --textDecoded--> AmtorEngine
//   transmit: ModemEngine + AmtorTxSource (100 baud, 170 Hz shift, CCIR 476,
//       each character sent twice, phasing signals first).
//
// On the amateur bands AMTOR goes in the RTTY / data segments.

#include "ModemEngine.h"

class NavtexWorker;
class QThread;

class AmtorEngine : public ModemEngine
{
    Q_OBJECT

public:
    explicit AmtorEngine(QObject *parent = nullptr);
    ~AmtorEngine() override;

    void loadSettings(QSettings *settings) override;
    bool reverse() const
    {
        return m_reverse;
    }
    bool locked() const
    {
        return m_locked;
    }
    double levelDb() const
    {
        return m_levelDb;
    }

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate) override;
    void setReverse(bool on);

protected:
    void applyConfig() override;
    void resetReceiver() override;
    QString prepareText(const QString &expanded) const override
    {
        return expanded.toUpper();
    }
    double prerollSeconds() const override
    {
        return 2.5; // phasing signals: the receiver locks on these
    }
    double tailSeconds() const override
    {
        return 0.0;
    }

private:
    QThread *m_thread = nullptr;
    NavtexWorker *m_worker = nullptr;
    bool m_reverse = false;
    bool m_locked = false;
    double m_levelDb = -120.0;
};
