#include "HellEngine.h"

#include "modemtxsource.h"

#include <QMetaType>
#include <QSettings>
#include <QThread>
#include <algorithm>
#include <cmath>

HellDecoderWorker::HellDecoderWorker(QObject *parent)
    : QObject(parent)
{
    m_statsTimer.start();
}

void HellDecoderWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    if (m_fs != sampleRate)
    {
        m_fs = sampleRate;
        m_demod.setSampleRate(m_fs);
    }
    m_cols.clear();
    m_demod.process(mono.constData(), size_t(mono.size()), m_cols);
    if (!m_cols.empty())
        emit columnsReady(QVector<float>(m_cols.begin(), m_cols.end()));
    if (m_statsTimer.elapsed() >= 250)
    {
        m_statsTimer.restart();
        emit levelUpdated(m_demod.levelDb());
    }
}

void HellDecoderWorker::setParams(double carrierHz, double trimPercent)
{
    m_demod.setCarrier(carrierHz);
    m_demod.setRateTrim(trimPercent);
}

void HellDecoderWorker::resetDecoder()
{
    m_demod.reset();
}

HellEngine::HellEngine(QObject *parent)
    : ModemEngine(QStringLiteral("Hell"), std::make_shared<HellTxSource>(), parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("hellDecoder"));
    m_worker = new HellDecoderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker,
            &HellDecoderWorker::columnsReady,
            this,
            [this](const QVector<float> &c)
            {
                if (decoding() && !transmitting())
                    emit columns(c);
            });
    connect(m_worker,
            &HellDecoderWorker::levelUpdated,
            this,
            [this](double db)
            {
                m_levelDb = db;
                emit statsChanged();
            });
    m_thread->start(QThread::LowPriority);
    applyConfig();
}

HellEngine::~HellEngine()
{
    m_thread->quit();
    m_thread->wait();
}

QStringList HellEngine::defaultMacroTexts() const
{
    QStringList t = ModemEngine::defaultMacroTexts();
    for (QString &s : t)
        s = s.toUpper();
    t[7] = QStringLiteral("RYRY HELL TEST DE {MYCALL} 0123456789\n");
    return t;
}

void HellEngine::loadSettings(QSettings *s)
{
    if (s)
        m_trim = std::clamp(s->value(QStringLiteral("Hell/RateTrim"), m_trim).toDouble(), -5.0, 5.0);
    ModemEngine::loadSettings(s);
}

void HellEngine::applyConfig()
{
    QMetaObject::invokeMethod(
        m_worker, "setParams", Qt::QueuedConnection, Q_ARG(double, m_carrierHz), Q_ARG(double, m_trim));
    txSource()->setCarrier(m_carrierHz);
    txSource()->setLevel(float(txLevel()));
}

void HellEngine::resetReceiver()
{
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
}

void HellEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!decoding())
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void HellEngine::setRateTrim(double percent)
{
    percent = std::clamp(percent, -5.0, 5.0);
    if (std::fabs(percent - m_trim) < 1e-6)
        return;
    m_trim = percent;
    save("RateTrim", m_trim);
    applyConfig();
    emit settingsChanged();
}
