#pragma once

// Transmit helpers:
//
//   Tune  - a steady tone (1750 Hz by default, like MMSSTV's tune tone) at the
//           tab's TX level, to set the TX level / ALC. Click again to stop.
//           The button is in the SSTV tab only.
//   CW ID - "DE {MYCALL}" (or your text) as keyed audio CW; also sent after a
//           transmission whose text ends in {CWID}, and after each SSTV
//           picture when that is ticked.
//
// Both are ModemEngines, so they go through MainWindow's one-transmitter rule,
// the PTT watchdogs and the TX time limit like any mode.
//
// MainWindow creates the one TxTools before the tabs; tabs reach it with
// TxTools::instance() (nullptr in a tab built without a MainWindow).

#include "ModemEngine.h"

#include <QObject>
#include <QWidget>
#include <functional>

class QPushButton;
class QSettings;

class TuneEngine : public ModemEngine
{
    Q_OBJECT

public:
    explicit TuneEngine(QObject *parent = nullptr);
    void startTune(double level, double hz);
    void stopTune(); // fades out, then releases
    void setTuneLevel(double level); // while the tone is on: follows the TX level slider

public slots:
    void pushAudio(const QVector<float> &, float) override { }

protected:
    void applyConfig() override;
    void resetReceiver() override { }
    double prerollSeconds() const override
    {
        return 0.0;
    }
    double tailSeconds() const override
    {
        return 0.0;
    }

private:
    double m_level = 0.1;
};

class CwIdEngine : public ModemEngine
{
    Q_OBJECT

public:
    explicit CwIdEngine(QObject *parent = nullptr);
    void sendId(const QString &text, double level, int wpm, double pitchHz);

public slots:
    void pushAudio(const QVector<float> &, float) override { }

protected:
    void applyConfig() override;
    void resetReceiver() override { }
    QString prepareText(const QString &expanded) const override
    {
        return expanded.toUpper();
    }
    double prerollSeconds() const override
    {
        return 0.2;
    }
    double tailSeconds() const override
    {
        return 0.2;
    }

private:
    double m_level = 0.1;
    int m_wpm = 20;
};

class TxTools : public QObject
{
    Q_OBJECT

public:
    explicit TxTools(QSettings *settings, QObject *parent = nullptr);
    ~TxTools() override;
    static TxTools *instance();

    TuneEngine *tuneEngine() const
    {
        return m_tune;
    }
    CwIdEngine *cwIdEngine() const
    {
        return m_cwId;
    }
    bool tuning() const;

    double tuneHz() const
    {
        return m_tuneHz;
    }
    QString cwIdText() const
    {
        return m_cwIdText;
    }
    int cwIdWpm() const
    {
        return m_cwIdWpm;
    }
    double cwIdPitch() const
    {
        return m_cwIdPitch;
    }

public slots:
    void startTune(double level);
    void stopTune();
    void setTuneLevel(double level);
    void sendCwId(double level);
    void setTuneHz(double hz);
    void setCwId(const QString &text, int wpm, double pitchHz);
    // Options dialog for the tune tone and the CW ID.
    void editSettings(QWidget *parent);

signals:
    void tuningChanged(bool on);
    void message(QString text); // for the status bar

private:
    QSettings *m_settings;
    TuneEngine *m_tune = nullptr;
    CwIdEngine *m_cwId = nullptr;
    double m_tuneHz = 1750.0;
    QString m_cwIdText;
    int m_cwIdWpm = 20;
    double m_cwIdPitch = 800.0;
};

// "CW ID" button for a tab's transmit row, plus "Tune" where withTune is set
// (the SSTV tab: 1750 Hz is SSTV's tune tone; the other modes have no common
// one). level() gives the tab's TX level when pressed.
class TxToolButtons : public QWidget
{
    Q_OBJECT

public:
    explicit TxToolButtons(std::function<double()> level, bool withTune = false, QWidget *parent = nullptr);
    void setTransmitAllowed(bool on);

public slots:
    // The tab's TX level slider moved: a Tune started here follows it at once.
    void levelChanged(double level);

private:
    void sync();

    std::function<double()> m_level;
    QPushButton *m_tune = nullptr;
    QPushButton *m_cwId = nullptr;
    bool m_allowed = false;
    bool m_ownTune = false; // this tab's Tune button started the tone
};
