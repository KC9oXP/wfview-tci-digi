#pragma once

// NAVTEX / SITOR-B receive (no transmit).
//
//   AudioHub::audio --pushAudio [queued]--> NavtexWorker (own QThread)
//       --textDecoded / statsUpdated [queued]--> NavtexEngine (GUI thread)

#include "navtexcore.h"

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

class QSettings;
class QThread;

class NavtexWorker : public QObject
{
    Q_OBJECT

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setParams(double centreHz, bool reverse);
    void resetDecoder();

signals:
    void textDecoded(QString text);
    void statsUpdated(bool locked, double levelDb);

private:
    NavtexDecoder m_dec;
    QElapsedTimer m_statsTimer;
};

class NavtexEngine : public QObject
{
    Q_OBJECT

public:
    explicit NavtexEngine(QSettings *settings, QObject *parent = nullptr);
    ~NavtexEngine() override;

    bool decoding() const
    {
        return m_decoding;
    }
    double centreHz() const
    {
        return m_centre;
    }
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
    void pushAudio(const QVector<float> &mono, float sampleRate);
    void setDecoding(bool on);
    void setCentreHz(double hz);
    void setReverse(bool on);

signals:
    void textAppended(QString text);
    void settingsChanged();
    void statsChanged();

private:
    void apply();

    QSettings *m_settings;
    QThread *m_thread = nullptr;
    NavtexWorker *m_worker = nullptr;
    bool m_decoding = false;
    double m_centre = 1000.0;
    bool m_reverse = false;
    bool m_locked = false;
    double m_levelDb = -120.0;
};
