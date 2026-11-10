#pragma once

// HF weather fax receive.
//
//   AudioHub::audio --pushAudio [queued]--> WefaxWorker (own QThread)
//       --rows / stateChanged / finished [queued]--> WefaxEngine (GUI thread)
//
// The picture grows row by row; it is saved as PNG in the wefax folder when
// the stop tone ends it (or Stop is pressed).

#include "wefaxcore.h"

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QVector>

class QSettings;
class QThread;

class WefaxWorker : public QObject
{
    Q_OBJECT

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setParams(double lpm, int ioc, bool autoStart, double slantPpm);
    void startManual();
    void stopImage();

signals:
    void rows(QByteArray grey, int width, int totalRows);
    void stateChanged(int state, int ioc, double periodRatio, int phasingLines);
    void started(int width);
    void finished();
    void statsUpdated(double aptHz, double levelDb);

private:
    WefaxDecoder m_dec;
    int m_sentRows = 0;
};

class WefaxEngine : public QObject
{
    Q_OBJECT

public:
    explicit WefaxEngine(QSettings *settings, QObject *parent = nullptr);
    ~WefaxEngine() override;

    bool decoding() const
    {
        return m_decoding;
    }
    double lpm() const
    {
        return m_lpm;
    }
    int ioc() const
    {
        return m_ioc;
    }
    bool autoStart() const
    {
        return m_auto;
    }
    double slantPpm() const
    {
        return m_ppm;
    }
    int state() const
    {
        return m_state;
    }
    const QImage &image() const
    {
        return m_img;
    }
    int rows() const
    {
        return m_rows;
    }
    double periodRatio() const
    {
        return m_ratio;
    }
    int phasingLines() const
    {
        return m_phasing;
    }
    double aptHz() const
    {
        return m_aptHz;
    }
    double levelDb() const
    {
        return m_levelDb;
    }

    QString save(const QImage &shown, const QString &note = QString());

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate);
    void setDecoding(bool on);
    void setLpm(double lpm);
    void setIoc(int ioc);
    void setAutoStart(bool on);
    void setSlantPpm(double ppm);
    void startManual();
    void stopImage();

signals:
    void settingsChanged();
    void imageChanged();
    void statusChanged();
    void pictureEnded(); // the panel saves it with its alignment
    void logLine(QString text);

private:
    void apply();

    QSettings *m_settings;
    QThread *m_thread = nullptr;
    WefaxWorker *m_worker = nullptr;
    bool m_decoding = false;
    double m_lpm = 120.0;
    int m_ioc = 576;
    bool m_auto = true;
    double m_ppm = 0.0;
    int m_state = 0;
    QImage m_img;
    int m_rows = 0;
    double m_ratio = 1.0;
    int m_phasing = 0;
    double m_aptHz = 0.0;
    double m_levelDb = -120.0;
};
