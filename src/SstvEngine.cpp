#include "SstvEngine.h"

#include "AppPaths.h"
#include "extratxsources.h"

#include <QDir>
#include <QMetaType>
#include <QPainter>
#include <QSettings>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------

QImage SstvDecoderWorker::snapshot() const
{
    const int w = m_dec.width(), h = m_dec.height();
    if (w <= 0 || h <= 0)
        return QImage();
    QImage img(w, h, QImage::Format_RGB32);
    const std::vector<uint32_t> &src = m_dec.image();
    for (int y = 0; y < h; ++y)
    {
        auto *row = reinterpret_cast<uint32_t *>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
            row[x] = 0xff000000u | (src[size_t(y) * size_t(w) + size_t(x)] & 0x00ffffffu);
    }
    return img;
}

void SstvDecoderWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    m_dec.setSampleRate(sampleRate);
    const SstvDecoder::Events ev = m_dec.process(mono.constData(), size_t(mono.size()));
    if (ev.finished && !ev.started)
    {
        emit imageFinished(snapshot(), m_dec.mode(), m_dec.linesDone());
        m_pending = false;
    }
    if (ev.started)
    {
        // A VIS can end the previous picture and begin a new one at once.
        emit imageStarted(m_dec.mode(), ev.noVis);
        m_pending = true;
    }
    if (ev.dirtyTo > 0 || ev.redraw)
        m_pending = true;
    if (m_pending && (!m_updateTimer.isValid() || m_updateTimer.elapsed() > 200))
    {
        m_updateTimer.restart();
        m_pending = false;
        emit imageUpdated(snapshot(), m_dec.linesDone(), m_dec.periodRatio());
    }
    if (!m_statsTimer.isValid() || m_statsTimer.elapsed() > 200)
    {
        m_statsTimer.restart();
        emit statsUpdated(
            m_dec.levelDb(), m_dec.lastHz(), m_dec.state() == SstvDecoder::State::Receiving, m_dec.offsetHz());
    }
}

void SstvDecoderWorker::setParams(bool autoSlant, double slantPpm, double offsetHz, bool autoOffset, bool autoStart)
{
    m_dec.setAutoSlant(autoSlant);
    m_dec.setSlantPpm(slantPpm);
    m_dec.setOffsetHz(offsetHz);
    m_dec.setAutoOffset(autoOffset);
    m_dec.setAutoStart(autoStart);
}

void SstvDecoderWorker::clearAutoOffset()
{
    m_dec.clearAutoOffset();
}

void SstvDecoderWorker::startManual(int mode)
{
    m_dec.startManual(mode);
    emit imageStarted(mode, false);
}

void SstvDecoderWorker::stopImage()
{
    if (m_dec.state() != SstvDecoder::State::Receiving)
        return;
    m_dec.stop();
    emit imageFinished(snapshot(), m_dec.mode(), m_dec.linesDone());
}

void SstvDecoderWorker::resetDecoder()
{
    m_dec.reset();
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

SstvEngine::SstvEngine(QObject *parent)
    : ModemEngine(QStringLiteral("SSTV"), std::make_shared<SstvTxSource>(), parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    m_carrierHz = 1900.0;
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("sstvDecoder"));
    m_worker = new SstvDecoderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker,
            &SstvDecoderWorker::imageStarted,
            this,
            [this](int mode, bool noVis)
            {
                m_receiving = true;
                m_rxMode = mode;
                setStatus(noVis ? tr("Receiving %1 (started from its sync pulses, no VIS)...")
                                      .arg(QLatin1String(sstv::info(mode).name))
                                : tr("Receiving %1...").arg(QLatin1String(sstv::info(mode).name)));
                if (noVis)
                    emit logLine(tr("[SSTV] %1 found from its sync timing (no VIS), receiving from here\n")
                                     .arg(QLatin1String(sstv::info(mode).name)));
                emit imageStarted(mode, noVis);
            });
    connect(m_worker,
            &SstvDecoderWorker::imageUpdated,
            this,
            [this](const QImage &img, int lines, double slant)
            {
                m_img = img;
                if (m_receiving && m_rxMode >= 0)
                    setStatus(tr("Receiving %1: line %2 of %3, timing %4 ppm")
                                  .arg(QLatin1String(sstv::info(m_rxMode).name))
                                  .arg(lines)
                                  .arg(sstv::info(m_rxMode).height)
                                  .arg(qRound((slant - 1.0) * 1e6)));
                emit imageUpdated(img, lines);
            });
    connect(m_worker,
            &SstvDecoderWorker::imageFinished,
            this,
            [this](const QImage &img, int mode, int lines)
            {
                m_receiving = false;
                m_img = img;
                emit imageUpdated(img, lines);
                if (mode < 0 || img.isNull())
                    return;
                const int h = sstv::info(mode).height;
                if (lines < h / 4)
                {
                    setStatus(tr("%1 picture lost after %2 lines (not saved).")
                                  .arg(QLatin1String(sstv::info(mode).name))
                                  .arg(lines));
                    return;
                }
                const QString path = saveImage(img, mode, lines < h ? QStringLiteral("partial") : QString());
                setStatus(tr("Received %1 (%2 of %3 lines), saved %4")
                              .arg(QLatin1String(sstv::info(mode).name))
                              .arg(lines)
                              .arg(h)
                              .arg(QDir::toNativeSeparators(path)));
                emit logLine(tr("[SSTV] received %1, %2 of %3 lines, saved %4\n")
                                 .arg(QLatin1String(sstv::info(mode).name))
                                 .arg(lines)
                                 .arg(h)
                                 .arg(QDir::toNativeSeparators(path)));
            });
    connect(m_worker,
            &SstvDecoderWorker::statsUpdated,
            this,
            [this](double db, double hz, bool receiving, double offset)
            {
                m_levelDb = db;
                m_hz = hz;
                m_totalOffset = offset;
                if (!receiving && m_receiving)
                    m_receiving = false;
                emit statsChanged();
            });
    m_thread->start(QThread::LowPriority);
    applyConfig();
}

SstvEngine::~SstvEngine()
{
    m_thread->quit();
    m_thread->wait();
}

void SstvEngine::loadSettings(QSettings *s)
{
    if (s)
    {
        s->beginGroup(group());
        m_autoSlant = s->value(QStringLiteral("AutoSlant"), m_autoSlant).toBool();
        m_slantPpm = std::clamp(s->value(QStringLiteral("SlantPpm"), m_slantPpm).toDouble(), -5000.0, 5000.0);
        m_txMode = std::clamp(s->value(QStringLiteral("TxMode"), m_txMode).toInt(), 0, int(sstv::ModeCount) - 1);
        m_offsetHz = std::clamp(s->value(QStringLiteral("OffsetHz"), m_offsetHz).toDouble(), -300.0, 300.0);
        m_totalOffset = m_offsetHz;
        m_autoOffset = s->value(QStringLiteral("AutoOffset"), m_autoOffset).toBool();
        m_autoStart = s->value(QStringLiteral("AutoStart"), m_autoStart).toBool();
        m_cwIdAfterPicture = s->value(QStringLiteral("CwIdAfter"), m_cwIdAfterPicture).toBool();
        m_trimOnTx = s->value(QStringLiteral("TrimOnTx"), m_trimOnTx).toBool();
        s->endGroup();
    }
    ModemEngine::loadSettings(s);
}

void SstvEngine::applyConfig()
{
    QMetaObject::invokeMethod(m_worker,
                              "setParams",
                              Qt::QueuedConnection,
                              Q_ARG(bool, m_autoSlant),
                              Q_ARG(double, m_slantPpm),
                              Q_ARG(double, m_offsetHz),
                              Q_ARG(bool, m_autoOffset),
                              Q_ARG(bool, m_autoStart));
    txSource()->setLevel(float(txLevel()));
}

void SstvEngine::setOffsetHz(double hz)
{
    // Set by hand (spin box, click, drag or wheel on the small waterfall): this
    // is where the signal is now. The automatic correction starts again from
    // zero, and the markers move at once, also with Decode off.
    hz = std::clamp(std::round(hz), -300.0, 300.0);
    if (hz == m_offsetHz && m_totalOffset == hz)
        return;
    m_offsetHz = hz;
    m_totalOffset = hz;
    save("OffsetHz", hz);
    applyConfig();
    QMetaObject::invokeMethod(m_worker, "clearAutoOffset", Qt::QueuedConnection);
    emit settingsChanged();
    emit statsChanged();
}

void SstvEngine::setAutoOffset(bool on)
{
    if (on == m_autoOffset)
        return;
    m_autoOffset = on;
    save("AutoOffset", on);
    applyConfig();
    emit settingsChanged();
}

void SstvEngine::setAutoStart(bool on)
{
    if (on == m_autoStart)
        return;
    m_autoStart = on;
    save("AutoStart", on);
    applyConfig();
    emit settingsChanged();
}

void SstvEngine::setCwIdAfter(bool on)
{
    if (on == m_cwIdAfterPicture)
        return;
    m_cwIdAfterPicture = on;
    save("CwIdAfter", on);
    emit settingsChanged();
}

void SstvEngine::resetReceiver()
{
    // A picture in progress is kept; nothing to flush.
}

void SstvEngine::pushAudio(const QVector<float> &mono, float sampleRate)
{
    if (!decoding() || transmitting())
        return;
    QMetaObject::invokeMethod(
        m_worker, "pushAudio", Qt::QueuedConnection, Q_ARG(QVector<float>, mono), Q_ARG(float, sampleRate));
}

void SstvEngine::setAutoSlant(bool on)
{
    if (on == m_autoSlant)
        return;
    m_autoSlant = on;
    save("AutoSlant", on);
    applyConfig();
    emit settingsChanged();
}

void SstvEngine::setTrimOnTx(bool on)
{
    if (on == m_trimOnTx)
        return;
    m_trimOnTx = on;
    save("TrimOnTx", on);
    emit settingsChanged();
}

void SstvEngine::setSlantPpm(double ppm)
{
    ppm = std::clamp(ppm, -5000.0, 5000.0);
    if (ppm == m_slantPpm)
        return;
    m_slantPpm = ppm;
    save("SlantPpm", ppm);
    applyConfig();
    emit settingsChanged();
}

void SstvEngine::setTxMode(int mode)
{
    mode = std::clamp(mode, 0, int(sstv::ModeCount) - 1);
    if (mode == m_txMode)
        return;
    m_txMode = mode;
    save("TxMode", mode);
    emit settingsChanged();
}

void SstvEngine::startManual(int mode)
{
    if (!decoding())
        setDecoding(true);
    QMetaObject::invokeMethod(m_worker, "startManual", Qt::QueuedConnection, Q_ARG(int, mode));
}

void SstvEngine::stopImage()
{
    QMetaObject::invokeMethod(m_worker, "stopImage", Qt::QueuedConnection);
}

QImage SstvEngine::fitToMode(const QImage &img, int mode)
{
    const sstv::ModeInfo &m = sstv::info(mode);
    QImage out(m.width, m.height, QImage::Format_RGB32);
    out.fill(Qt::black);
    if (img.isNull())
        return out;
    // Shown at 4:3 whatever the pixel grid: fill a 4:3 frame, cropping the excess.
    const double frameAspect = 4.0 / 3.0;
    const double srcAspect = double(img.width()) / img.height();
    QRectF src(0, 0, img.width(), img.height());
    if (srcAspect > frameAspect)
    {
        const double w = img.height() * frameAspect;
        src = QRectF((img.width() - w) / 2.0, 0, w, img.height());
    }
    else
    {
        const double h = img.width() / frameAspect;
        src = QRectF(0, (img.height() - h) / 2.0, img.width(), h);
    }
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(QRectF(0, 0, m.width, m.height), img, src);
    return out;
}

void SstvEngine::sendImage(const QImage &img)
{
    if (transmitting())
        return;
    const sstv::ModeInfo &m = sstv::info(m_txMode);
    const QImage fitted = (img.width() == m.width && img.height() == m.height)
        ? img.convertToFormat(QImage::Format_RGB32)
        : fitToMode(img, m_txMode);
    std::vector<uint32_t> rgb(size_t(m.width) * size_t(m.height));
    for (int y = 0; y < m.height; ++y)
        std::memcpy(&rgb[size_t(y) * size_t(m.width)], fitted.constScanLine(y), size_t(m.width) * 4);
    for (uint32_t &v : rgb)
        v &= 0x00ffffffu;
    auto src = std::static_pointer_cast<SstvTxSource>(txSource());
    src->setTimingPpm(m_trimOnTx ? m_slantPpm : 0.0);
    src->setImage(m_txMode, rgb);
    startTx();
    if (transmitting())
    {
        finishTx(); // release when the picture is out
        if (m_cwIdAfterPicture)
            requestCwIdAfter();
        setStatus(tr("Sending %1 (%2 s)").arg(QLatin1String(m.name)).arg(qRound(sstv::imageSeconds(m_txMode) + 0.9)));
        emit logLine(tr("[SSTV] sending %1\n").arg(QLatin1String(m.name)));
    }
}

double SstvEngine::txProgress() const
{
    return std::static_pointer_cast<SstvTxSource>(txSource())->progress();
}

double SstvEngine::txSeconds() const
{
    return std::static_pointer_cast<SstvTxSource>(txSource())->totalSeconds();
}

int SstvEngine::txLine() const
{
    return std::static_pointer_cast<SstvTxSource>(txSource())->lineSending();
}

int SstvEngine::txLines() const
{
    return std::static_pointer_cast<SstvTxSource>(txSource())->imageLines();
}

QString SstvEngine::saveImage(const QImage &img, int mode, const QString &note)
{
    if (img.isNull())
        return QString();
    QString name = AppPaths::stamp() + QLatin1Char('_')
        + AppPaths::safe(QLatin1String(sstv::info(mode).name)).replace(QLatin1Char(' '), QLatin1Char('-'));
    if (!note.isEmpty())
        name += QLatin1Char('_') + note;
    const QString path = QDir(AppPaths::sstv()).filePath(name + QStringLiteral(".png"));
    // Saved at 4:3 so it looks as it did on screen.
    const QImage out = img.scaled(img.width(), img.width() * 3 / 4, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (!out.save(path))
        return QString();
    emit imageSaved(path, out);
    return path;
}
