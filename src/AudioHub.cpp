#include "AudioHub.h"

#include "AppPaths.h"
#include "wavfile.h"

#include <QDir>
#include <QFileInfo>
#include <algorithm>

AudioHub::AudioHub(QObject *parent)
    : QObject(parent)
{
    m_playTimer.setInterval(20);
    connect(&m_playTimer, &QTimer::timeout, this, &AudioHub::playTick);
}

AudioHub::~AudioHub()
{
    stopRecording();
}

void AudioHub::setRingSeconds(int sec)
{
    m_ringSec = std::clamp(sec, 10, 1800);
    m_ring.clear();
    m_ringPos = m_ringFill = 0;
}

double AudioHub::bufferedSeconds() const
{
    return m_ringRate > 0 ? double(m_ringFill) / m_ringRate : 0.0;
}

void AudioHub::toRing(const float *x, int n, float rate)
{
    if (rate != m_ringRate || m_ring.empty())
    {
        m_ringRate = rate;
        m_ring.assign(size_t(double(m_ringSec) * rate), 0.0f);
        m_ringPos = m_ringFill = 0;
    }
    const size_t cap = m_ring.size();
    for (int i = 0; i < n; ++i)
    {
        m_ring[m_ringPos] = x[i];
        if (++m_ringPos == cap)
            m_ringPos = 0;
    }
    m_ringFill = std::min(cap, m_ringFill + size_t(n));
}

void AudioHub::fromRadio(const QVector<float> &mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    toRing(mono.constData(), int(mono.size()), sampleRate);
    if (m_rec)
    {
        m_rec->write(mono.constData(), size_t(mono.size()));
        if (!m_recNotify.isValid() || m_recNotify.elapsed() > 1000)
        {
            m_recNotify.restart();
            emit recordingChanged(true, m_rec->seconds());
        }
    }
    if (!m_playing)
        emit audio(mono, sampleRate);
}

QString AudioHub::saveLast(int seconds, QString *err)
{
    if (m_ringFill == 0 || m_ringRate <= 0)
    {
        if (err)
            *err = tr("No receive audio yet.");
        return QString();
    }
    const size_t want = std::min(m_ringFill, size_t(double(seconds) * m_ringRate));
    const QString path = QDir(AppPaths::recordings())
                             .filePath(QStringLiteral("rx-last%1s-%2.wav").arg(seconds).arg(AppPaths::stamp()));
    WavWriter w;
    if (!w.open(path.toUtf8().toStdString(), int(m_ringRate)))
    {
        if (err)
            *err = tr("Cannot write %1").arg(path);
        return QString();
    }
    const size_t cap = m_ring.size();
    size_t start = (m_ringPos + cap - want) % cap;
    size_t left = want;
    while (left > 0)
    {
        const size_t chunk = std::min(left, cap - start);
        w.write(m_ring.data() + start, chunk);
        left -= chunk;
        start = (start + chunk) % cap;
    }
    w.close();
    return path;
}

bool AudioHub::startRecording(QString *err)
{
    stopRecording();
    if (m_ringRate <= 0)
    {
        if (err)
            *err = tr("No receive audio yet: connect first.");
        return false;
    }
    m_recPath = QDir(AppPaths::recordings()).filePath(QStringLiteral("rx-%1.wav").arg(AppPaths::stamp()));
    m_rec = std::make_unique<WavWriter>();
    if (!m_rec->open(m_recPath.toUtf8().toStdString(), int(m_ringRate)))
    {
        m_rec.reset();
        if (err)
            *err = tr("Cannot write %1").arg(m_recPath);
        return false;
    }
    m_recNotify.restart();
    emit recordingChanged(true, 0.0);
    return true;
}

void AudioHub::stopRecording()
{
    if (!m_rec)
        return;
    const double sec = m_rec->seconds();
    m_rec->close();
    m_rec.reset();
    emit recordingChanged(false, sec);
}

bool AudioHub::recording() const
{
    return m_rec != nullptr;
}

bool AudioHub::play(const QString &path, double speed, QString *err)
{
    stopPlayback();
    std::string e;
    double rate = 0.0;
    if (!wav::read(path.toUtf8().toStdString(), m_play, rate, e))
    {
        if (err)
            *err = QString::fromStdString(e);
        return false;
    }
    m_playRate = rate;
    m_playSpeed = std::clamp(speed, 0.25, 32.0);
    m_playPos = 0;
    m_playPath = path;
    m_playing = true;
    m_playClock.start();
    m_playTimer.start();
    emit playbackChanged(true, 0.0);
    return true;
}

void AudioHub::stopPlayback()
{
    if (!m_playing)
        return;
    m_playTimer.stop();
    m_playing = false;
    m_play.clear();
    m_play.shrink_to_fit();
    emit playbackChanged(false, 1.0);
}

void AudioHub::playTick()
{
    if (!m_playing)
        return;
    // Keep pace with the clock (times speed), in chunks of at most 100 ms of audio.
    const size_t due = size_t(double(m_playClock.elapsed()) / 1000.0 * m_playRate * m_playSpeed);
    const size_t maxChunk = size_t(0.1 * m_playRate);
    while (m_playPos < due && m_playPos < m_play.size())
    {
        const size_t n = std::min({due - m_playPos, maxChunk, m_play.size() - m_playPos});
        QVector<float> chunk(static_cast<qsizetype>(n));
        std::copy(
            m_play.begin() + std::ptrdiff_t(m_playPos), m_play.begin() + std::ptrdiff_t(m_playPos + n), chunk.begin());
        m_playPos += n;
        emit audio(chunk, float(m_playRate));
    }
    emit playbackChanged(true, m_play.empty() ? 1.0 : double(m_playPos) / double(m_play.size()));
    if (m_playPos >= m_play.size())
        stopPlayback();
}
