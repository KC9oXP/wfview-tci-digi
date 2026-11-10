#pragma once

// Everything decoded (and sent), as plain text, one file per mode per UTC
// day in the logs folder ("2026-11-10 PSK.txt"), so modes decoding at the
// same time do not interleave. A header line marks each change of
// frequency or settings, or a pause of more than two minutes:
//
//   --- 2026-11-10 18:40:12Z  14.070.000 Hz  DIGU  PSK BPSK31 @ 1000 Hz ---

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QObject>
#include <QString>
#include <memory>

class QSettings;

class SessionLog : public QObject
{
    Q_OBJECT

public:
    explicit SessionLog(QSettings *settings, QObject *parent = nullptr);
    ~SessionLog() override;

    bool enabled() const
    {
        return m_enabled;
    }
    void setEnabled(bool on);
    QString pathFor(const QString &mode) const; // today's file for a mode
    QString folder() const;

    void setRadio(qint64 hz, const QString &modulation);
    // mode: the file ("PSK"); detail: the settings in the header ("PSK BPSK31 @ 1000 Hz")
    void add(const QString &mode, const QString &detail, const QString &text);

private:
    struct Stream
    {
        QFile file;
        QString day;
        QString lastDetail;
        qint64 lastHz = -1;
        QDateTime lastWrite;
        bool atLineStart = true;
    };

    QSettings *m_settings;
    bool m_enabled = true;
    qint64 m_hz = 0;
    QString m_modulation;
    QHash<QString, std::shared_ptr<Stream>> m_streams;
};
