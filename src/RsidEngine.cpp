#include "RsidEngine.h"

#include <QMetaType>
#include <QSettings>
#include <QThread>

void RsidWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    if (m_fs != double(sampleRate))
    {
        m_fs = sampleRate;
        m_det.setSampleRate(m_fs);
        m_det.setRange(200.0, 3000.0);
    }
    for (const rsid::Hit &h : m_det.process(mono.constData(), size_t(mono.size())))
        emit found(h.code, h.centreHz, h.mirrored, h.errors);
}

void RsidWorker::resetDetector()
{
    m_det.reset();
}

RsidEngine::RsidEngine(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    if (m_settings)
    {
        m_enabled = m_settings->value(QStringLiteral("RSID/Enabled"), true).toBool();
        m_autoSwitch = m_settings->value(QStringLiteral("RSID/AutoSwitch"), true).toBool();
        m_goBack = m_settings->value(QStringLiteral("RSID/GoBack"), true).toBool();
        m_fixSideband = m_settings->value(QStringLiteral("RSID/FixSideband"), true).toBool();
    }
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("rsid"));
    m_worker = new RsidWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker,
            &RsidWorker::found,
            this,
            [this](int code, double hz, bool mirrored, int errors)
            {
                if (m_enabled && !m_transmitting)
                    emit detected(code, hz, mirrored, errors);
            });
    m_thread->start(QThread::LowPriority);
}

RsidEngine::~RsidEngine()
{
    m_thread->quit();
    m_thread->wait();
}

void RsidEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!m_enabled || m_transmitting)
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void RsidEngine::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (m_settings)
        m_settings->setValue(QStringLiteral("RSID/Enabled"), on);
    QMetaObject::invokeMethod(m_worker, "resetDetector", Qt::QueuedConnection);
}

void RsidEngine::setAutoSwitch(bool on)
{
    m_autoSwitch = on;
    if (m_settings)
        m_settings->setValue(QStringLiteral("RSID/AutoSwitch"), on);
}

void RsidEngine::setGoBack(bool on)
{
    m_goBack = on;
    if (m_settings)
        m_settings->setValue(QStringLiteral("RSID/GoBack"), on);
}

void RsidEngine::setFixSideband(bool on)
{
    m_fixSideband = on;
    if (m_settings)
        m_settings->setValue(QStringLiteral("RSID/FixSideband"), on);
}

void RsidEngine::setTransmitting(bool on)
{
    if (on == m_transmitting)
        return;
    m_transmitting = on;
    if (!on)
        QMetaObject::invokeMethod(m_worker, "resetDetector", Qt::QueuedConnection);
}
