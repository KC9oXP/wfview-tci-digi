#include "SessionLog.h"

#include "AppPaths.h"

#include <QDir>
#include <QSettings>

SessionLog::SessionLog(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_enabled = m_settings->value(QStringLiteral("Log/Session"), true).toBool();
}

SessionLog::~SessionLog() = default;

void SessionLog::setEnabled(bool on)
{
    m_enabled = on;
    m_settings->setValue(QStringLiteral("Log/Session"), on);
    if (!on)
        m_streams.clear();
}

QString SessionLog::folder() const
{
    return AppPaths::logs();
}

QString SessionLog::pathFor(const QString &mode) const
{
    const QString day = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd"));
    return QDir(AppPaths::logs()).filePath(QStringLiteral("%1 %2.txt").arg(day, AppPaths::safe(mode)));
}

void SessionLog::setRadio(qint64 hz, const QString &modulation)
{
    m_hz = hz;
    m_modulation = modulation;
}

void SessionLog::add(const QString &mode, const QString &detail, const QString &text)
{
    if (!m_enabled || text.isEmpty())
        return;
    std::shared_ptr<Stream> &s = m_streams[mode];
    if (!s)
        s = std::make_shared<Stream>();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString day = now.toString(QStringLiteral("yyyy-MM-dd"));
    if (!s->file.isOpen() || s->day != day)
    {
        s->file.close();
        s->day = day;
        s->file.setFileName(pathFor(mode));
        s->lastDetail.clear();
        if (!s->file.open(QIODevice::Append | QIODevice::Text))
            return;
    }
    const bool paused = !s->lastWrite.isValid() || s->lastWrite.secsTo(now) > 120;
    if (detail != s->lastDetail || m_hz != s->lastHz || paused)
    {
        QString f = QString::number(m_hz);
        for (int i = f.size() - 3; i > 0; i -= 3)
            f.insert(i, QLatin1Char('.'));
        const QString head = QStringLiteral("%1--- %2  %3 Hz  %4  %5 ---\n")
                                 .arg(s->atLineStart ? QString() : QStringLiteral("\n"),
                                      now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss'Z'")),
                                      f,
                                      m_modulation,
                                      detail);
        s->file.write(head.toUtf8());
        s->lastDetail = detail;
        s->lastHz = m_hz;
        s->atLineStart = true;
    }
    s->file.write(text.toUtf8());
    s->file.flush();
    s->atLineStart = text.endsWith(QLatin1Char('\n'));
    s->lastWrite = now;
}
