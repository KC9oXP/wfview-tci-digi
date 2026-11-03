#pragma once

// RTTY (Baudot FSK) receive and AFSK transmit.
//
//   TciClient::rxAudio --pushAudio [queued]--> RttyDecoderWorker (own QThread)
//       --textDecoded / statsUpdated / tonesFound / scopeUpdated [queued]--> RttyEngine (GUI thread)
//
//   RttyEngine --> RttyTxSource <-- TciClient TX render callback
//   RttyEngine -- transmitRequested(bool) --> TciClient::setTransmit()
//
// Transmit sends audio tones (AFSK) as TCI TX audio, so the radio must be in a
// data/SSB mode that takes computer audio (USB-D / DIGU on Icom).

#include "rttycore.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <memory>
#include <vector>

class QSettings;
class QThread;
class RttyTxSource;

class RttyDecoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit RttyDecoderWorker(QObject *parent = nullptr);

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setParams(double baud,
                   double shiftHz,
                   double markHz,
                   bool reverse,
                   double stopBits,
                   bool usos,
                   bool ita2,
                   double squelch);
    void resetDecoder();
    void findTones(double shiftHz);

signals:
    void textDecoded(QString text);
    void statsUpdated(double quality, double markDb, double spaceDb, bool squelchOpen, double sampleRate);
    void tonesFound(bool found, double lowHz, double highHz, double levelDb);
    // Crossed-ellipse scope points, flat [x0, y0, x1, y1, ...] in -1..1, ~15 frames a second.
    void scopeUpdated(QVector<float> xy);

private:
    void applyConfig();

    RttyConfig m_cfg;
    double m_squelch = 0.0;
    RttyDemodulator m_demod;
    RttyScope m_scope;
    std::vector<RttyScope::Point> m_scopePts;
    QElapsedTimer m_scopeTimer;
    std::vector<float> m_history;
    QElapsedTimer m_statsTimer;
};

class RttyEngine : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMacroCount = 8;

    enum Sideband
    {
        SidebandAuto = 0, // from the radio's mode (USB/DIGU -> USB, LSB/DIGL -> LSB)
        SidebandUsb = 1,
        SidebandLsb = 2
    };

    explicit RttyEngine(QObject *parent = nullptr);
    ~RttyEngine() override;

    void loadSettings(QSettings *settings);
    std::shared_ptr<RttyTxSource> txSource() const
    {
        return m_tx;
    }

    // ---- settings
    bool decoding() const
    {
        return m_decoding;
    }
    double baud() const
    {
        return m_baud;
    }
    int shift() const
    {
        return m_shift;
    }
    int markHz() const
    {
        return m_markHz;
    }
    int spaceHz() const;
    int sideband() const
    {
        return m_sideband;
    }
    bool usbEffective() const; // the sideband actually used
    bool reverse() const
    {
        return m_reverse;
    }
    double stopBits() const
    {
        return m_stopBits;
    }
    bool usos() const
    {
        return m_usos;
    }
    bool ita2() const
    {
        return m_ita2;
    }
    double squelch() const
    {
        return m_squelch;
    }
    bool diddle() const
    {
        return m_diddle;
    }
    double txLevel() const
    {
        return m_txLevel;
    }
    QString myCall() const
    {
        return m_myCall;
    }
    QString call() const
    {
        return m_call;
    }
    QStringList macroNames() const
    {
        return m_macroNames;
    }
    QStringList macroTexts() const
    {
        return m_macroTexts;
    }
    bool holdTx() const
    {
        return m_holdTx;
    }
    bool transmitting() const
    {
        return m_keyed;
    }
    QString status() const
    {
        return m_status;
    }

    // ---- stats
    double quality() const
    {
        return m_quality;
    }
    double markDb() const
    {
        return m_markDb;
    }
    double spaceDb() const
    {
        return m_spaceDb;
    }
    bool squelchOpen() const
    {
        return m_squelchOpen;
    }

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate);

    void setDecoding(bool on);
    void setBaud(double baud);
    void setShift(int hz);
    void setMarkHz(int hz);
    void setSideband(int sb);
    void setReverse(bool on);
    void setStopBits(double bits);
    void setUsos(bool on);
    void setIta2(bool on);
    void setSquelch(double q);
    void setDiddle(bool on);
    void setTxLevel(double level);
    void setMyCall(const QString &c);
    void setCall(const QString &c);
    void setMacro(int index, const QString &name, const QString &text);
    void setHoldTx(bool on);

    void resync();
    void autoTune();

    // From the TCI client.
    void setConnected(bool ready, bool canTransmit);
    void setRadioMode(const QString &mode);
    void setRadioTransmitting(bool on);

    // Transmit.
    void send(const QString &text);
    void sendMacro(int index);
    void startTx(); // key up and hold until finishTx() / abortTx()
    void finishTx(); // release once everything queued has been sent
    void abortTx();

signals:
    void settingsChanged();
    void macrosChanged();
    void statsChanged();
    void statusChanged(QString status);
    void transmittingChanged(bool on);
    void textAppended(QString text);
    void scopePoints(QVector<float> xy);
    void transmitRequested(bool on);

private slots:
    void onTextDecoded(const QString &text);
    void onStatsUpdated(double quality, double markDb, double spaceDb, bool squelchOpen, double sampleRate);
    void onTonesFound(bool found, double lowHz, double highHz, double levelDb);
    void onScopeUpdated(const QVector<float> &xy);
    void pollTx();

private:
    RttyConfig config() const;
    void applyConfig();
    void save(const char *key, const QVariant &value);
    void saveMacros();
    QString expand(const QString &text) const;
    void keyUp();
    void release(const QString &why);
    void setStatus(const QString &s);

    QThread *m_thread = nullptr;
    RttyDecoderWorker *m_worker = nullptr;
    std::shared_ptr<RttyTxSource> m_tx;
    QSettings *m_settings = nullptr;

    bool m_decoding = false;
    bool m_ready = false;
    bool m_canTransmit = false;
    double m_baud = 45.45;
    int m_shift = 170;
    int m_markHz = 2295; // USB: mark 2295, space 2125
    int m_sideband = SidebandAuto;
    QString m_radioMode;
    bool m_reverse = false;
    double m_stopBits = 1.5;
    bool m_usos = true;
    bool m_ita2 = false;
    double m_squelch = 0.5;
    bool m_diddle = false;
    double m_txLevel = 0.5;
    QString m_myCall;
    QString m_call;
    QStringList m_macroNames;
    QStringList m_macroTexts;

    double m_quality = 0.0;
    double m_markDb = 0.0;
    double m_spaceDb = 0.0;
    bool m_squelchOpen = false;
    QString m_status;
    bool m_atLineStart = true;

    // transmit
    bool m_keyed = false;
    bool m_holdTx = false;
    bool m_autoRelease = false;
    bool m_tailQueued = false;
    bool m_radioTx = false;
    bool m_radioWasTx = false;
    QElapsedTimer m_keyedTimer;
    QTimer m_txPoll;
};
