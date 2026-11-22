#pragma once

// RSID receive on every tab: watches the receive audio for the ID burst
// fldigi and other programs send before a transmission (engine/rsidcore.h).
//
//   AudioHub::audio --pushAudio [queued]--> RsidWorker (own QThread)
//       --found [queued]--> RsidEngine (GUI thread) --detected-->

#include "rsidcore.h"

#include <QObject>
#include <QVector>

class QSettings;
class QThread;

class RsidWorker : public QObject
{
    Q_OBJECT

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void resetDetector();

signals:
    void found(int code, double centreHz, bool mirrored, int errors);

private:
    rsid::Detector m_det;
    double m_fs = 0.0;
};

class RsidEngine : public QObject
{
    Q_OBJECT

public:
    RsidEngine(QSettings *settings, QObject *parent = nullptr);
    ~RsidEngine() override;

    // Options > RSID: listen for IDs (default on).
    bool enabled() const
    {
        return m_enabled;
    }
    // Options > RSID: switch tab, mode and frequency when one is heard (default on).
    bool autoSwitch() const
    {
        return m_autoSwitch;
    }
    // Options > RSID: go back to the previous tab when the signal ends (default on).
    bool goBack() const
    {
        return m_goBack;
    }

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate);
    void setEnabled(bool on);
    void setAutoSwitch(bool on);
    void setGoBack(bool on);
    // No listening while transmitting (our own ID, if we ever send one).
    void setTransmitting(bool on);

signals:
    void detected(int code, double centreHz, bool mirrored, int errors);

private:
    QSettings *m_settings;
    QThread *m_thread = nullptr;
    RsidWorker *m_worker = nullptr;
    bool m_enabled = true;
    bool m_autoSwitch = true;
    bool m_goBack = true;
    bool m_transmitting = false;
};
