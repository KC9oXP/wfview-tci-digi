#include "SpectrumEngine.h"

#include <QMetaType>
#include <QThread>

void SpectrumWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    if (m_an.sampleRate() != double(sampleRate))
        m_an.setSampleRate(sampleRate);
    m_lines.clear();
    m_an.push(mono.constData(), size_t(mono.size()), m_lines);
    for (const std::vector<float> &l : m_lines)
        emit line(QVector<float>(l.begin(), l.end()), m_an.hzPerBin());
}

SpectrumEngine::SpectrumEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("spectrum"));
    m_worker = new SpectrumWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SpectrumWorker::line, this, &SpectrumEngine::line);
    m_thread->start(QThread::LowPriority);
}

SpectrumEngine::~SpectrumEngine()
{
    m_thread->quit();
    m_thread->wait();
}

void SpectrumEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!m_enabled)
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}
