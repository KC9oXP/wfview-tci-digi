#pragma once

// Transmit helpers:
//
//   Tune  - a steady tone (1750 Hz by default, like MMSSTV's tune tone) at the
//           tab's TX level, to set the TX level / ALC. Click again to stop.
//           The button is in the SSTV tab only.
//   Auto level - keys a short test signal shaped like the tab's mode (RTTY: mark
//           and space alternating; other modes: one tone at the tab's audio
//           frequency, which has the same peaks) and searches for the highest
//           TX level whose ALC reading is at or below a target (0.1 by
//           default, Options > Tune tone, CW ID and Auto level). Target 0: it
//           first finds where ALC shows, then backs off until it reads 0.
//           The result is set on the tab's TX level slider.
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
#include "autolevel.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

class QPushButton;
class QSettings;

class TuneEngine : public ModemEngine
{
    Q_OBJECT

public:
    explicit TuneEngine(QObject *parent = nullptr);
    // hz2 > 0: alternate hz and hz2 every holdSec (an RTTY-like mark/space signal).
    void startTune(double level, double hz, double hz2 = 0.0, double holdSec = 0.0);
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

    // ---- auto TX level
    struct TestSignal
    {
        double hz = 1500.0;
        double hz2 = 0.0; // > 0: alternate with hz every holdSec (RTTY)
        double holdSec = 0.0;
    };
    bool autoLeveling() const
    {
        return m_al != nullptr;
    }
    double autoLevelTarget() const
    {
        return m_alTarget;
    }
    void setAutoLevelTarget(double alc);
    // Start the search from 'level' (0..1); apply(level) is called with the result.
    void startAutoLevel(double level, const TestSignal &sig, std::function<void(double)> apply);
    void cancelAutoLevel();
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
    // From the TCI client, for Auto level.
    void onTxSensors(double alc, double power, double swr);
    void setRadioTransmitting(bool on);
    void setCwId(const QString &text, int wpm, double pitchHz);
    // Options dialog for the tune tone and the CW ID.
    void editSettings(QWidget *parent);

signals:
    void tuningChanged(bool on);
    void autoLevelChanged(bool on);
    void message(QString text); // for the status bar

private:
    QSettings *m_settings;
    TuneEngine *m_tune = nullptr;
    CwIdEngine *m_cwId = nullptr;
    double m_tuneHz = 1750.0;
    QString m_cwIdText;
    int m_cwIdWpm = 20;
    double m_cwIdPitch = 800.0;

    void autoLevelTick();
    void finishAutoLevel(const QString &text, bool applyResult, double resultDb);
    std::unique_ptr<autolevel::Search> m_al;
    std::function<void(double)> m_alApply;
    double m_alTarget = 0.1;
    QTimer m_alTimer;
    QElapsedTimer m_alTotal; // since the start
    QElapsedTimer m_alSince; // since the level was last changed
    QElapsedTimer m_alTxSince; // since the radio went to transmit
    bool m_radioTx = false;
    std::vector<double> m_alReadings;
    double m_alLastAlc = 0.0;
};

// "CW ID" button for a tab's transmit row, plus "Tune" where withTune is set
// (the SSTV tab: 1750 Hz is SSTV's tune tone; the other modes have no common
// one). level() gives the tab's TX level when pressed.
class TxToolButtons : public QWidget
{
    Q_OBJECT

public:
    // level: the tab's TX level now; setLevel: set it (Auto level's result);
    // testSignal: the test signal for Auto level, shaped like the tab's mode.
    TxToolButtons(std::function<double()> level,
                  std::function<void(double)> setLevel,
                  std::function<TxTools::TestSignal()> testSignal,
                  bool withTune = false,
                  QWidget *parent = nullptr);
    void setTransmitAllowed(bool on);

public slots:
    // The tab's TX level slider moved: a Tune started here follows it at once.
    void levelChanged(double level);

private:
    void sync();

    std::function<double()> m_level;
    std::function<void(double)> m_setLevel;
    std::function<TxTools::TestSignal()> m_signal;
    QPushButton *m_tune = nullptr;
    QPushButton *m_auto = nullptr;
    QPushButton *m_cwId = nullptr;
    bool m_allowed = false;
    bool m_ownTune = false; // this tab's Tune button started the tone
};
