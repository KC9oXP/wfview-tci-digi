#pragma once

// SSTV receive and transmit.
//
//   AudioHub::audio --pushAudio [queued]--> SstvDecoderWorker (own QThread)
//       --imageStarted / imageUpdated / imageFinished [queued]--> SstvEngine
//
// Received pictures are saved as PNG in the sstv folder when they end.
// Transmit goes through ModemEngine (PTT, watchdogs): sendImage() keys up,
// the SstvTxSource sends the VIS header and the lines, and the radio is
// released when the picture is out.

#include "ModemEngine.h"
#include "sstvcore.h"

#include <QElapsedTimer>
#include <QImage>

class QThread;

class SstvDecoderWorker : public QObject
{
    Q_OBJECT

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setParams(bool autoSlant, double slantPpm);
    void startManual(int mode);
    void stopImage();
    void resetDecoder();

signals:
    void imageStarted(int mode);
    void imageUpdated(QImage img, int linesDone, double slant);
    void imageFinished(QImage img, int mode, int linesDone);
    void statsUpdated(double levelDb, double hz, bool receiving);

private:
    QImage snapshot() const;
    SstvDecoder m_dec;
    QElapsedTimer m_updateTimer, m_statsTimer;
    bool m_pending = false;
};

class SstvEngine : public ModemEngine
{
    Q_OBJECT

public:
    explicit SstvEngine(QObject *parent = nullptr);
    ~SstvEngine() override;

    void loadSettings(QSettings *settings) override;

    bool autoSlant() const
    {
        return m_autoSlant;
    }
    double slantPpm() const
    {
        return m_slantPpm;
    }
    int txMode() const
    {
        return m_txMode;
    }
    bool receiving() const
    {
        return m_receiving;
    }
    int rxMode() const
    {
        return m_rxMode;
    }
    QImage lastImage() const
    {
        return m_img;
    }
    double levelDb() const
    {
        return m_levelDb;
    }
    double toneHz() const
    {
        return m_hz;
    }
    double txProgress() const;
    double txSeconds() const;

    // Scales img to the mode's size (filling it, cropped to fit).
    static QImage fitToMode(const QImage &img, int mode);

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate) override;
    void setAutoSlant(bool on);
    void setSlantPpm(double ppm);
    void setTxMode(int mode);
    void startManual(int mode);
    void stopImage();
    // img must already be the mode's size (fitToMode, overlay drawn).
    void sendImage(const QImage &img);
    QString saveImage(const QImage &img, int mode, const QString &note = QString());

signals:
    void imageStarted(int mode);
    void imageUpdated(QImage img, int linesDone);
    void imageSaved(QString path, QImage img);
    void logLine(QString text); // for the session log

protected:
    void applyConfig() override;
    void resetReceiver() override;
    double prerollSeconds() const override
    {
        return 0.0;
    }
    double tailSeconds() const override
    {
        return 0.2;
    }

private:
    QThread *m_thread = nullptr;
    SstvDecoderWorker *m_worker = nullptr;
    bool m_autoSlant = true;
    double m_slantPpm = 0.0;
    int m_txMode = sstv::Martin1;
    bool m_receiving = false;
    int m_rxMode = -1;
    QImage m_img;
    double m_levelDb = -120.0;
    double m_hz = 0.0;
};
