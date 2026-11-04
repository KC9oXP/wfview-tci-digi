#pragma once

// Receive spectrum for the waterfalls, computed on its own thread and shared
// by the PSK and Hell tabs. Runs whenever connected, whatever the Decode
// buttons say.

#include "spectrum.h"

#include <QObject>
#include <QVector>

class QThread;

class SpectrumWorker : public QObject
{
    Q_OBJECT

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);

signals:
    void line(QVector<float> db, double hzPerBin);

private:
    SpectrumAnalyzer m_an;
    std::vector<std::vector<float>> m_lines;
};

class SpectrumEngine : public QObject
{
    Q_OBJECT

public:
    explicit SpectrumEngine(QObject *parent = nullptr);
    ~SpectrumEngine() override;

public slots:
    void pushAudio(const QVector<float> &mono, float sampleRate);
    void setEnabled(bool on)
    {
        m_enabled = on;
    }

signals:
    void line(QVector<float> db, double hzPerBin);

private:
    QThread *m_thread = nullptr;
    SpectrumWorker *m_worker = nullptr;
    bool m_enabled = true;
};
