#pragma once

// Common controller for the PSK and Feld Hell modes (GUI thread): receive
// on/off, carrier frequency, macros, call signs and the transmit state
// machine with the same watchdogs as the RTTY engine.
//
//   ModemEngine --> ModemTxSource <-- TciClient TX render callback (via MainWindow)
//   ModemEngine -- transmitRequested(bool) --> TciClient::setTransmit()
//
// A subclass owns the receive worker thread and the mode's own settings.

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVector>
#include <memory>

class ModemTxSource;
class QSettings;

class ModemEngine : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMacroCount = 8;

    ModemEngine(const QString &group, std::shared_ptr<ModemTxSource> tx, QObject *parent = nullptr);
    ~ModemEngine() override;

    virtual void loadSettings(QSettings *settings);
    std::shared_ptr<ModemTxSource> txSource() const
    {
        return m_tx;
    }
    QString group() const
    {
        return m_group;
    }

    bool decoding() const
    {
        return m_decoding;
    }
    double carrierHz() const
    {
        return m_carrierHz;
    }
    // Audio passband the carrier may be set in.
    static constexpr double kMinHz = 200.0;
    static constexpr double kMaxHz = 3000.0;
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
    bool canTransmit() const
    {
        return m_canTransmit;
    }
    QString status() const
    {
        return m_status;
    }

public slots:
    virtual void pushAudio(const QVector<float> &mono, float sampleRate) = 0;

    void setDecoding(bool on);
    void setCarrierHz(double hz);
    void setTxLevel(double level);
    void setMyCall(const QString &c);
    void setCall(const QString &c);
    void setMacro(int index, const QString &name, const QString &text);
    void setHoldTx(bool on);

    void setConnected(bool ready, bool canTransmit);
    void setRadioTransmitting(bool on);

    void send(const QString &text);
    void sendMacro(int index);
    void startTx();
    void finishTx();
    void abortTx();

signals:
    void settingsChanged();
    void macrosChanged();
    void statsChanged();
    void statusChanged(QString status);
    void transmittingChanged(bool on);
    void textAppended(QString text);
    void transmitRequested(bool on);

protected:
    // Hooks for the mode.
    virtual void applyConfig() = 0; // push settings to the worker and the TX source
    virtual void resetReceiver() = 0;
    virtual void onDecodingChanged(bool on);
    virtual QString prepareText(const QString &expanded) const
    {
        return expanded;
    }
    virtual double prerollSeconds() const
    {
        return 0.5;
    }
    virtual double tailSeconds() const
    {
        return 0.3;
    }
    virtual QStringList defaultMacroNames() const;
    virtual QStringList defaultMacroTexts() const;

    void save(const char *key, const QVariant &value);
    void setStatus(const QString &s);
    // Called by a subclass when AFC moves the receive frequency; no settings write.
    void trackCarrier(double hz);
    // Text decoded by the worker, shown unless transmitting.
    void deliverText(const QString &text);
    QSettings *settings() const
    {
        return m_settings;
    }
    bool ready() const
    {
        return m_ready;
    }

    double m_carrierHz = 1000.0;

private slots:
    void pollTx();

private:
    QString expand(const QString &text) const;
    void keyUp();
    void release(const QString &why);
    void saveMacros();
    void initMacros();

    QString m_group;
    std::shared_ptr<ModemTxSource> m_tx;
    QSettings *m_settings = nullptr;

    bool m_decoding = false;
    bool m_ready = false;
    bool m_canTransmit = false;
    double m_txLevel = 0.5;
    QString m_myCall;
    QString m_call;
    QStringList m_macroNames;
    QStringList m_macroTexts;
    QString m_status;
    bool m_atLineStart = true;

    bool m_keyed = false;
    bool m_holdTx = false;
    bool m_autoRelease = false;
    bool m_tailQueued = false;
    bool m_radioTx = false;
    bool m_radioWasTx = false;
    QElapsedTimer m_keyedTimer;
    QTimer m_txPoll;
};
