#pragma once

// WSPR receive and beacon transmit, in 2-minute slots on even UTC minutes.
//
//   AudioHub::audio --pushAudio [queued, with its arrival time]--> WsprWorker
//       (own QThread: resample to 12 kHz, keep the last 130 s)
//   slot clock (GUI thread, PC time): at 1:55.5 of a slot the worker decodes
//       the audio from 0:00 - 1 s to 1:54 --slotDone [queued]--> WsprEngine
//
// Transmit goes through ModemEngine (PTT, watchdogs): at 0:01 of a slot chosen
// by the TX percentage the WsprTxSource sends the 110.6 s message, and the
// radio is released when it is out.
//
// Band hopping: at 1:56 the next slot's band is picked (HopPlan) and
// tuneRequested is sent. A slot is only decoded if the radio stayed on one
// dial, did not transmit and Decode was on for all of it.
//
// Free offsets: every decoded slot (spots and the averaged spectrum) goes to
// a FreeFinder for the dial; it suggests clear transmit offsets and can pick
// one before each transmission.

#include "ModemEngine.h"
#include "audioresampler.h"
#include "wsprcore.h"

#include <QDateTime>
#include <QMetaType>
#include <QStringList>
#include <QTimer>
#include <deque>
#include <map>
#include <vector>

class QThread;

struct WsprSlot
{
    qint64 slotMs = 0; // UTC start of the slot (even minute)
    qint64 dialHz = 0;
    std::vector<wspr::Spot> spots;
    wspr::WindowSpectrum spectrum;
    double decodeSec = 0.0;
    double missingSec = 0.0; // of the 115 s recording (app started late, audio gap)
    bool valid = false;
};
Q_DECLARE_METATYPE(WsprSlot)

class WsprWorker : public QObject
{
    Q_OBJECT

public slots:
    void pushAudio(QVector<float> mono, float sampleRate, double arrivalEpochMs);
    void decodeSlot(qint64 slotMs, qint64 dialHz, int passes);
    void loadHashes(QString text);
    void reset();

signals:
    void slotDone(WsprSlot s);
    void hashesChanged(QString text);

private:
    AudioResampler m_rs;
    double m_fs = 0.0;
    std::vector<float> m_ring; // 12 kHz, the last kHistorySec
    qint64 m_count = 0; // 12 kHz samples received in all
    std::deque<std::pair<double, double>> m_t0; // (arrival ms, estimate of the epoch ms of sample 0)
    double m_lastArrival = 0.0;
    wspr::Decoder m_dec;
    size_t m_knownCalls = 0;
    static constexpr double kHistorySec = 130.0;
};

class WsprEngine : public ModemEngine
{
    Q_OBJECT

public:
    explicit WsprEngine(QObject *parent = nullptr);
    ~WsprEngine() override;

    void loadSettings(QSettings *settings) override;

    // Station (the call is ModemEngine's shared My call).
    QString grid() const
    {
        return m_grid;
    }
    int dbm() const
    {
        return m_dbm;
    }
    // What will be sent, in turn (empty with an error when it cannot be).
    QStringList messages(QString *err = nullptr) const;

    bool txEnabled() const
    {
        return m_txEnabled;
    }
    int txPercent() const
    {
        return m_sched.percent();
    }
    double txOffsetHz() const
    {
        return m_txOffset;
    }
    bool autoFree() const
    {
        return m_autoFree;
    }
    bool txNextForced() const
    {
        return m_forceNext;
    }
    int passes() const
    {
        return m_passes;
    }
    // Power from the radio's own reading (servers that report watts): after each
    // transmission Power is set from what the radio put out, less the feed line.
    bool autoPower() const
    {
        return m_autoPower;
    }
    double feedLossDb() const
    {
        return m_feedLossDb;
    }
    QString powerNote() const
    {
        return m_powerNote;
    }
    wspr::HopPlan hopPlan() const
    {
        return m_hop;
    }
    // Transmit and band hopping only happen while the WSPR tab is shown.
    bool active() const
    {
        return m_active;
    }

    // Slot clock: UTC start of the slot now, and seconds into it.
    static qint64 slotStart(qint64 utcMs);
    qint64 currentSlot() const
    {
        return m_slot;
    }
    double secondsIntoSlot() const;
    // What this slot is: "Receiving", "Transmitting", "Not decoded (...)".
    QString slotState() const
    {
        return m_slotState;
    }
    bool nextIsTx() const
    {
        return m_nextTx;
    }
    double txProgress() const;
    double lastTxCentre() const
    {
        return m_lastTxCentre;
    }

    // Free offsets on the current dial.
    std::vector<wspr::FreeFinder::Busy> busy() const;
    std::vector<wspr::FreeFinder::Pick> suggestions() const;
    double clearance(double centreHz) const;
    int slotsHeard() const;
    const wspr::WindowSpectrum &lastSpectrum() const
    {
        return m_lastSpectrum;
    }

    // Median DT of the last decodes: a hint that the PC clock is off.
    bool clockHint(double &medianDt, int &count) const;

    qint64 radioDial() const
    {
        return m_dial;
    }

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate) override;
    void setGrid(const QString &grid);
    void setDbm(int dbm);
    void setTxEnabled(bool on);
    void setTxPercent(int pct);
    void setTxOffsetHz(double hz);
    void setAutoFree(bool on);
    void setPasses(int passes);
    void setAutoPower(bool on);
    void setFeedLossDb(double db);
    // TCI tx_sensors output power (W), while the radio transmits.
    void addTxPower(double watts);
    void setHopPlan(const wspr::HopPlan &plan);
    void txNextSlot(); // the next slot transmits (once), whatever the percentage
    void setActive(bool shown);
    // From the TCI client.
    void setRadio(qint64 dialHz, const QString &mode, bool transmitting);

signals:
    void slotDecoded(WsprSlot s); // after every decoded slot (spots may be empty)
    void slotChanged(); // a new slot began, or its state changed
    void freeChanged(); // busy ranges / suggestions changed
    void tuneRequested(qint64 dialHz, QString why); // band hop
    void logLine(QString text);

protected:
    void applyConfig() override;
    void resetReceiver() override;
    void onDecodingChanged(bool on) override;
    double prerollSeconds() const override
    {
        return 0.0;
    }
    double tailSeconds() const override
    {
        return 0.1;
    }

private slots:
    void tick();
    void onSlotDone(WsprSlot s);
    void onTransmitting(bool on);

private:
    void beginSlot(qint64 slot);
    void planNext(qint64 nextSlot);
    void startBeacon();
    void invalidate(const QString &why);
    void saveHop();
    wspr::FreeFinder &finder();
    const wspr::FreeFinder *finderFor(qint64 dial) const;

    QThread *m_thread = nullptr;
    WsprWorker *m_worker = nullptr;
    QTimer m_tick;

    QString m_grid;
    int m_dbm = 30;
    bool m_txEnabled = false;
    double m_txOffset = 1500.0;
    bool m_autoFree = true;
    int m_passes = 2;
    bool m_autoPower = false;
    double m_feedLossDb = 0.0;
    QString m_powerNote;
    wspr::PowerMeter m_meter;
    QElapsedTimer m_txClock;
    wspr::HopPlan m_hop;
    wspr::TxScheduler m_sched;
    bool m_active = false;

    qint64 m_slot = 0;
    bool m_slotRx = false; // this slot is being recorded for decoding
    bool m_slotTx = false; // this slot transmits
    bool m_txStarted = false;
    bool m_decodeAsked = false;
    bool m_planned = false;
    bool m_nextTx = false;
    bool m_forceNext = false;
    int m_msgIndex = 0;
    double m_lastTxCentre = 0.0;
    QString m_slotState;
    qint64 m_slotDial = 0;
    qint64 m_hopTarget = 0; // dial asked for by the last band hop, until the radio is there

    qint64 m_dial = 0;
    QString m_mode;
    bool m_radioTx = false;

    std::map<qint64, wspr::FreeFinder> m_free; // per dial
    wspr::WindowSpectrum m_lastSpectrum;
    std::deque<double> m_recentDt;
};
