#pragma once

// Feld Hell receive (painted columns) and transmit.
//
//   TciClient::rxAudio --pushAudio [queued]--> HellDecoderWorker (own QThread)
//       --columnsReady [queued]--> HellEngine --columns--> HellWidget

#include "ModemEngine.h"
#include "hellcore.h"

#include <QElapsedTimer>

class QThread;

class HellDecoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit HellDecoderWorker(QObject *parent = nullptr);

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    void setParams(double carrierHz, double trimPercent);
    void resetDecoder();

signals:
    // n columns of hell::kRows values each, bottom first, 0..1.
    void columnsReady(QVector<float> cols);
    void levelUpdated(double levelDb);

private:
    HellDemodulator m_demod;
    double m_fs = 0.0;
    std::vector<float> m_cols;
    QElapsedTimer m_statsTimer;
};

class HellEngine : public ModemEngine
{
    Q_OBJECT

public:
    explicit HellEngine(QObject *parent = nullptr);
    ~HellEngine() override;

    void loadSettings(QSettings *settings) override;
    double rateTrim() const
    {
        return m_trim;
    }
    double levelDb() const
    {
        return m_levelDb;
    }

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate) override;
    void setRateTrim(double percent);

signals:
    void columns(QVector<float> cols);

protected:
    void applyConfig() override;
    void resetReceiver() override;
    QString prepareText(const QString &expanded) const override
    {
        return expanded.toUpper(); // the font has capitals only
    }
    QStringList defaultMacroTexts() const override;
    double prerollSeconds() const override
    {
        return 0.4;
    }
    double tailSeconds() const override
    {
        return 0.4;
    }

private:
    QThread *m_thread = nullptr;
    HellDecoderWorker *m_worker = nullptr;
    double m_trim = 0.0;
    double m_levelDb = -120.0;
};
