#include "NavtexEngine.h"

#include <QMetaType>
#include <QSettings>
#include <QThread>
#include <algorithm>

void NavtexWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    m_dec.setSampleRate(sampleRate);
    const std::string out = m_dec.process(mono.constData(), size_t(mono.size()));
    if (!out.empty())
        emit textDecoded(QString::fromLatin1(out.data(), int(out.size())));
    if (!m_statsTimer.isValid() || m_statsTimer.elapsed() > 300)
    {
        m_statsTimer.restart();
        emit statsUpdated(m_dec.locked(), m_dec.levelDb());
    }
}

void NavtexWorker::setParams(double centreHz, bool reverse)
{
    m_dec.setCentreHz(centreHz);
    m_dec.setReverse(reverse);
}

void NavtexWorker::resetDecoder()
{
    m_dec.reset();
}

NavtexEngine::NavtexEngine(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_centre = std::clamp(m_settings->value(QStringLiteral("NAVTEX/Centre"), 1000.0).toDouble(), 300.0, 2700.0);
    m_reverse = m_settings->value(QStringLiteral("NAVTEX/Reverse"), false).toBool();
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("navtexDecoder"));
    m_worker = new NavtexWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &NavtexWorker::textDecoded, this, [this](const QString &t) { emit textAppended(t); });
    connect(m_worker,
            &NavtexWorker::statsUpdated,
            this,
            [this](bool locked, double db)
            {
                m_locked = locked;
                m_levelDb = db;
                emit statsChanged();
            });
    m_thread->start(QThread::LowPriority);
    apply();
}

NavtexEngine::~NavtexEngine()
{
    m_thread->quit();
    m_thread->wait();
}

void NavtexEngine::apply()
{
    QMetaObject::invokeMethod(
        m_worker, "setParams", Qt::QueuedConnection, Q_ARG(double, m_centre), Q_ARG(bool, m_reverse));
}

void NavtexEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!m_decoding)
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void NavtexEngine::setDecoding(bool on)
{
    if (on == m_decoding)
        return;
    m_decoding = on;
    if (on)
        QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
    m_locked = false;
    emit settingsChanged();
    emit statsChanged();
}

void NavtexEngine::setCentreHz(double hz)
{
    hz = std::clamp(hz, 300.0, 2700.0);
    if (hz == m_centre)
        return;
    m_centre = hz;
    m_settings->setValue(QStringLiteral("NAVTEX/Centre"), m_centre);
    apply();
    emit settingsChanged();
}

void NavtexEngine::setReverse(bool on)
{
    if (on == m_reverse)
        return;
    m_reverse = on;
    m_settings->setValue(QStringLiteral("NAVTEX/Reverse"), on);
    apply();
    emit settingsChanged();
}
