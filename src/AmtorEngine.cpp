#include "AmtorEngine.h"

#include "NavtexEngine.h"
#include "extratxsources.h"

#include <QMetaType>
#include <QSettings>
#include <QThread>
#include <algorithm>

AmtorEngine::AmtorEngine(QObject *parent)
    : ModemEngine(QStringLiteral("AMTOR"), std::make_shared<AmtorTxSource>(), parent)
{
    setDecodeOnSend(true);
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_carrierHz = 1500.0;
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("amtorDecoder"));
    m_worker = new NavtexWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &NavtexWorker::textDecoded, this, [this](const QString &t) { deliverText(t); });
    connect(m_worker,
            &NavtexWorker::statsUpdated,
            this,
            [this](bool locked, double db)
            {
                if (!decoding())
                    return;
                const bool changed = locked != m_locked;
                m_locked = locked;
                m_levelDb = db;
                if (changed && !transmitting())
                    setStatus(locked ? tr("Receiving (locked)") : tr("Listening..."));
                emit statsChanged();
            });
    m_thread->start(QThread::LowPriority);
    applyConfig();
}

AmtorEngine::~AmtorEngine()
{
    m_thread->quit();
    m_thread->wait();
}

void AmtorEngine::loadSettings(QSettings *s)
{
    if (s)
    {
        s->beginGroup(group());
        m_reverse = s->value(QStringLiteral("Reverse"), m_reverse).toBool();
        if (!s->contains(QStringLiteral("Carrier")))
            m_carrierHz = 1500.0;
        s->endGroup();
    }
    ModemEngine::loadSettings(s);
}

void AmtorEngine::applyConfig()
{
    // The whole signal (centre +/- 85 Hz plus keying sidebands) inside the passband.
    m_carrierHz = std::clamp(m_carrierHz, kMinHz + 150.0, kMaxHz - 150.0);
    QMetaObject::invokeMethod(
        m_worker, "setParams", Qt::QueuedConnection, Q_ARG(double, m_carrierHz), Q_ARG(bool, m_reverse));
    auto tx = std::static_pointer_cast<AmtorTxSource>(txSource());
    tx->setCarrier(m_carrierHz);
    tx->setReverse(m_reverse);
    tx->setLevel(float(txLevel()));
}

void AmtorEngine::resetReceiver()
{
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
}

void AmtorEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!decoding() || transmitting())
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void AmtorEngine::setReverse(bool on)
{
    if (on == m_reverse)
        return;
    m_reverse = on;
    save("Reverse", on);
    applyConfig();
    resetReceiver();
    emit settingsChanged();
}
