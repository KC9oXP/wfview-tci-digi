#include "WsprNet.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimeZone>
#include <QUrl>

WsprNet::WsprNet(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_net = new QNetworkAccessManager(this);
    if (m_settings)
        m_enabled = m_settings->value(QStringLiteral("WSPR/WsprnetUpload"), false).toBool();
}

QString WsprNet::versionTag()
{
    return QStringLiteral("TCIDigi-" APP_VERSION);
}

void WsprNet::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (m_settings)
        m_settings->setValue(QStringLiteral("WSPR/WsprnetUpload"), on);
    if (!on)
        m_queue.clear();
    m_lastError.clear();
    emit statusChanged();
}

QString WsprNet::statusText() const
{
    if (!m_enabled)
        return tr("WSPRnet upload off");
    QString s = tr("WSPRnet: %n spot(s) sent", "", m_sent);
    if (!m_queue.isEmpty())
        s += tr(", %1 waiting").arg(m_queue.size());
    if (!m_lastError.isEmpty())
        s += tr(" - last error: %1").arg(m_lastError);
    return s;
}

void WsprNet::submit(qint64 slotUtcMs, qint64 rxDialHz, const std::vector<wspr::Spot> &spots, const QString &myCall,
                     const QString &myGrid, int txPercent, int dbm)
{
    if (!m_enabled || rxDialHz <= 0)
        return;
    const QString call = myCall.trimmed().toUpper();
    const QString grid = myGrid.trimmed();
    if (call.isEmpty() || !wspr::validGrid(grid.toStdString()))
    {
        m_lastError = tr("set your call and grid in the WSPR tab");
        emit statusChanged();
        return;
    }
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(slotUtcMs, QTimeZone::utc());
    const int yymmdd = (t.date().year() % 100) * 10000 + t.date().month() * 100 + t.date().day();
    const int hhmm = t.time().hour() * 100 + t.time().minute();
    const std::string ver = versionTag().toStdString();
    int n = 0;
    for (const wspr::Spot &s : spots)
    {
        // A Type 3 message whose call is not known yet cannot be reported.
        if (s.call.empty() || s.call[0] == '<')
            continue;
        m_queue.enqueue(QString::fromStdString(
            wspr::wsprnetSpotQuery(call.toStdString(), grid.toStdString(), rxDialHz, s, yymmdd, hhmm, ver)));
        ++n;
    }
    if (n == 0)
        m_queue.enqueue(QString::fromStdString(wspr::wsprnetStatusQuery(
            call.toStdString(), grid.toStdString(), rxDialHz, txPercent, rxDialHz, dbm, ver)));
    emit statusChanged();
    next();
}

void WsprNet::next()
{
    if (m_busy || m_queue.isEmpty())
        return;
    m_busy = true;
    const QString q = m_queue.dequeue();
    QNetworkRequest req(QUrl(m_base + QLatin1Char('?') + q));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("wfview-tci-digi/" APP_VERSION));
    req.setTransferTimeout(20000);
    QNetworkReply *r = m_net->get(req);
    connect(r,
            &QNetworkReply::finished,
            this,
            [this, r, q]()
            {
                r->deleteLater();
                m_busy = false;
                const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (r->error() == QNetworkReply::NoError && (http == 0 || (http >= 200 && http < 400)))
                {
                    m_lastError.clear();
                    if (q.startsWith(QLatin1String("function=wspr&")))
                        ++m_sent;
                    m_sentQueries << q;
                    if (m_sentQueries.size() > 200)
                        m_sentQueries.removeFirst();
                }
                else
                {
                    ++m_failed;
                    m_lastError = http ? tr("HTTP %1").arg(http) : r->errorString();
                    emit message(tr("WSPRnet: upload failed (%1).").arg(m_lastError));
                }
                emit statusChanged();
                next();
            });
}
