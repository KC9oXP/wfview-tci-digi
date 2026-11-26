#pragma once

// WSPRnet spot upload (Options in the WSPR tab, off by default).
//
// After each received slot: one "function=wspr" request per decoded spot, or
// one "function=wsprstat" (station alive, no spots) when nothing was heard, to
// https://wsprnet.org/post. Requests go out one after another.

#include "wsprcore.h"

#include <QObject>
#include <QQueue>
#include <QString>
#include <vector>

class QNetworkAccessManager;
class QSettings;

class WsprNet : public QObject
{
    Q_OBJECT

public:
    explicit WsprNet(QSettings *settings, QObject *parent = nullptr);

    bool enabled() const
    {
        return m_enabled;
    }
    int spotsSent() const
    {
        return m_sent;
    }
    int waiting() const
    {
        return int(m_queue.size());
    }
    QString lastError() const
    {
        return m_lastError;
    }
    QString statusText() const;

    static QString versionTag(); // "TCIDigi-0.9.17"

    // Query strings sent (for the e2e test).
    QStringList sentQueries() const
    {
        return m_sentQueries;
    }
    // The e2e test points this at a local server.
    void setBaseUrl(const QString &url)
    {
        m_base = url;
    }

public slots:
    void setEnabled(bool on);
    // One received slot. slotUtcMs: start of the slot (even minute).
    void submit(qint64 slotUtcMs, qint64 rxDialHz, const std::vector<wspr::Spot> &spots, const QString &myCall,
                const QString &myGrid, int txPercent, int dbm);

signals:
    void statusChanged();
    void message(const QString &line); // for the TCI log

private:
    void next();

    QSettings *m_settings = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    bool m_enabled = false;
    bool m_busy = false;
    QQueue<QString> m_queue;
    QString m_base = QStringLiteral("https://wsprnet.org/post");
    QString m_lastError;
    int m_sent = 0;
    int m_failed = 0;
    QStringList m_sentQueries;
};
