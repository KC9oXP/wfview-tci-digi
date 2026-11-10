#include "WefaxEngine.h"

#include "AppPaths.h"

#include <QDir>
#include <QMetaType>
#include <QSettings>
#include <QThread>
#include <algorithm>
#include <cstring>

void WefaxWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    m_dec.setSampleRate(sampleRate);
    const WefaxDecoder::Events ev = m_dec.process(mono.constData(), size_t(mono.size()));
    if (ev.started)
    {
        m_sentRows = 0;
        emit started(m_dec.width());
    }
    const int have = m_dec.rows();
    if (have > m_sentRows)
    {
        const int w = m_dec.width();
        const auto *src = reinterpret_cast<const char *>(m_dec.image().data()) + size_t(m_sentRows) * size_t(w);
        emit rows(QByteArray(src, (have - m_sentRows) * w), w, have);
        m_sentRows = have;
    }
    if (ev.stateChanged)
        emit stateChanged(int(m_dec.state()), m_dec.ioc(), m_dec.periodRatio(), m_dec.phasingLines());
    if (ev.finished)
        emit finished();
    static thread_local int tick = 0;
    if (++tick % 10 == 0)
        emit statsUpdated(m_dec.aptHz(), m_dec.levelDb());
}

void WefaxWorker::setParams(double lpm, int ioc, bool autoStart, double slantPpm)
{
    m_dec.setLpm(lpm);
    if (m_dec.state() == WefaxDecoder::State::Idle)
        m_dec.setIoc(ioc);
    m_dec.setAutoStart(autoStart);
    m_dec.setSlantPpm(slantPpm);
}

void WefaxWorker::startManual()
{
    m_dec.startManual();
    m_sentRows = 0;
    emit started(m_dec.width());
    emit stateChanged(int(m_dec.state()), m_dec.ioc(), m_dec.periodRatio(), 0);
}

void WefaxWorker::stopImage()
{
    const bool wasImage = m_dec.state() == WefaxDecoder::State::Image;
    m_dec.stop();
    emit stateChanged(int(m_dec.state()), m_dec.ioc(), m_dec.periodRatio(), m_dec.phasingLines());
    if (wasImage)
        emit finished();
}

WefaxEngine::WefaxEngine(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_lpm = m_settings->value(QStringLiteral("WEFAX/LPM"), 120.0).toDouble();
    m_ioc = m_settings->value(QStringLiteral("WEFAX/IOC"), 576).toInt();
    m_auto = m_settings->value(QStringLiteral("WEFAX/AutoStart"), true).toBool();
    m_ppm = m_settings->value(QStringLiteral("WEFAX/SlantPpm"), 0.0).toDouble();
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("wefaxDecoder"));
    m_worker = new WefaxWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker,
            &WefaxWorker::started,
            this,
            [this](int width)
            {
                m_img = QImage(width, 600, QImage::Format_Grayscale8);
                m_img.fill(255);
                m_rows = 0;
                emit imageChanged();
                emit logLine(tr("[WEFAX] picture started\n"));
            });
    connect(m_worker,
            &WefaxWorker::rows,
            this,
            [this](const QByteArray &grey, int width, int total)
            {
                if (m_img.isNull() || m_img.width() != width)
                {
                    m_img = QImage(width, 600, QImage::Format_Grayscale8);
                    m_img.fill(255);
                    m_rows = 0;
                }
                const int n = int(grey.size() / width);
                if (m_rows + n > m_img.height())
                {
                    QImage bigger(width, std::max(m_img.height() * 2, m_rows + n), QImage::Format_Grayscale8);
                    bigger.fill(255);
                    for (int y = 0; y < m_rows; ++y)
                        std::memcpy(bigger.scanLine(y), m_img.constScanLine(y), size_t(width));
                    m_img = bigger;
                }
                for (int i = 0; i < n; ++i)
                    std::memcpy(
                        m_img.scanLine(m_rows + i), grey.constData() + size_t(i) * size_t(width), size_t(width));
                m_rows += n;
                Q_UNUSED(total);
                emit imageChanged();
            });
    connect(m_worker,
            &WefaxWorker::stateChanged,
            this,
            [this](int st, int ioc, double ratio, int phasing)
            {
                m_state = st;
                m_ratio = ratio;
                m_phasing = phasing;
                if (ioc != m_ioc && st != 0)
                {
                    m_ioc = ioc; // from the APT start tone
                    emit settingsChanged();
                }
                emit statusChanged();
            });
    connect(m_worker,
            &WefaxWorker::finished,
            this,
            [this]()
            {
                emit pictureEnded();
                emit statusChanged();
            });
    connect(m_worker,
            &WefaxWorker::statsUpdated,
            this,
            [this](double apt, double db)
            {
                m_aptHz = apt;
                m_levelDb = db;
                emit statusChanged();
            });
    m_thread->start(QThread::LowPriority);
    apply();
}

WefaxEngine::~WefaxEngine()
{
    m_thread->quit();
    m_thread->wait();
}

void WefaxEngine::apply()
{
    QMetaObject::invokeMethod(m_worker,
                              "setParams",
                              Qt::QueuedConnection,
                              Q_ARG(double, m_lpm),
                              Q_ARG(int, m_ioc),
                              Q_ARG(bool, m_auto),
                              Q_ARG(double, m_ppm));
}

void WefaxEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!m_decoding)
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void WefaxEngine::setDecoding(bool on)
{
    if (on == m_decoding)
        return;
    m_decoding = on;
    emit settingsChanged();
    emit statusChanged();
}

void WefaxEngine::setLpm(double lpm)
{
    if (lpm == m_lpm)
        return;
    m_lpm = lpm;
    m_settings->setValue(QStringLiteral("WEFAX/LPM"), lpm);
    apply();
    emit settingsChanged();
}

void WefaxEngine::setIoc(int ioc)
{
    if (ioc == m_ioc)
        return;
    m_ioc = ioc;
    m_settings->setValue(QStringLiteral("WEFAX/IOC"), ioc);
    apply();
    emit settingsChanged();
}

void WefaxEngine::setAutoStart(bool on)
{
    if (on == m_auto)
        return;
    m_auto = on;
    m_settings->setValue(QStringLiteral("WEFAX/AutoStart"), on);
    apply();
    emit settingsChanged();
}

void WefaxEngine::setSlantPpm(double ppm)
{
    if (ppm == m_ppm)
        return;
    m_ppm = ppm;
    m_settings->setValue(QStringLiteral("WEFAX/SlantPpm"), ppm);
    apply();
    emit settingsChanged();
}

void WefaxEngine::startManual()
{
    if (!m_decoding)
        setDecoding(true);
    QMetaObject::invokeMethod(m_worker, "startManual", Qt::QueuedConnection);
}

void WefaxEngine::stopImage()
{
    QMetaObject::invokeMethod(m_worker, "stopImage", Qt::QueuedConnection);
}

QString WefaxEngine::save(const QImage &shown, const QString &note)
{
    if (shown.isNull() || shown.height() < 2)
        return QString();
    QString name = AppPaths::stamp() + QStringLiteral("_wefax");
    if (!note.isEmpty())
        name += QLatin1Char('_') + note;
    const QString path = QDir(AppPaths::wefax()).filePath(name + QStringLiteral(".png"));
    if (!shown.save(path))
        return QString();
    emit logLine(tr("[WEFAX] %1 rows saved %2\n").arg(shown.height()).arg(QDir::toNativeSeparators(path)));
    return path;
}
