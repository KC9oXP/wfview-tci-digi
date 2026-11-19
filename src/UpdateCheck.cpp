#include "UpdateCheck.h"

#include "versioncheck.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVariant>

namespace
{
const char *const releasesApi = "https://api.github.com/repos/KC9oXP/wfview-tci-digi/releases?per_page=30";
const char *const releasesPage = "https://github.com/KC9oXP/wfview-tci-digi/releases";
constexpr int startDelayMs = 4000; // let the window come up and the TCI connection start first
constexpr int timeoutMs = 15000;

// Release pages are only ever opened on github.com, whatever the reply says.
bool trustedUrl(const QString &url)
{
    const QUrl u(url);
    return u.isValid() && u.scheme() == QLatin1String("https") && u.host() == QLatin1String("github.com")
        && u.path().startsWith(QLatin1String("/KC9oXP/wfview-tci-digi/"));
}
} // namespace

UpdateCheck::UpdateCheck(QSettings *settings, QMainWindow *window)
    : QObject(window)
    , m_settings(settings)
    , m_window(window)
{
    m_running = QStringLiteral(APP_VERSION);
    // "--pretend-version 0.4.0" (see main.cpp) makes the check behave as if that version were
    // running, so the notice can be tried without waiting for a real release.
    const QString pretend = qApp->property("pretendVersion").toString();
    if (!pretend.isEmpty())
    {
        m_running = pretend;
        m_pretend = true;
    }
}

bool UpdateCheck::automatic() const
{
    return m_settings->value(QStringLiteral("Update/Auto"), true).toBool();
}

void UpdateCheck::setAutomatic(bool on)
{
    m_settings->setValue(QStringLiteral("Update/Auto"), on);
}

void UpdateCheck::startAutomatic()
{
    if (!automatic())
        return;
    if (versioncheck::parse(m_running.toStdString()).empty())
        return; // test builds have no version number
    QTimer::singleShot(startDelayMs, this, [this]() { request(false); });
}

void UpdateCheck::checkNow()
{
    request(true);
}

void UpdateCheck::request(bool manual)
{
    if (m_busy)
        return;
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkRequest req {QUrl(QString::fromLatin1(releasesApi))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", QByteArray("wfview-tci-digi/") + QByteArray(APP_VERSION));
    req.setTransferTimeout(timeoutMs);
    m_busy = true;
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual]() { finished(reply, manual); });
}

void UpdateCheck::finished(QNetworkReply *reply, bool manual)
{
    m_busy = false;
    reply->deleteLater();

    QString error;
    QString bestTag;
    QString bestUrl;
    QString bestDownload;
    QString bestAsset;
    std::vector<int> best;
    if (reply->error() != QNetworkReply::NoError)
    {
        error = reply->errorString();
    }
    else
    {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isArray())
        {
            error = tr("GitHub's answer could not be read.");
        }
        else
        {
            // The highest version among the published releases (drafts are not public).
            const QJsonArray list = doc.array();
            for (const QJsonValue &v : list)
            {
                const QJsonObject o = v.toObject();
                if (o.value(QStringLiteral("draft")).toBool())
                    continue;
                const QString tag = o.value(QStringLiteral("tag_name")).toString();
                const std::vector<int> ver = versioncheck::parse(tag.toStdString());
                if (ver.empty())
                    continue;
                if (best.empty() || versioncheck::compare(ver, best) > 0)
                {
                    best = ver;
                    bestTag = tag;
                    bestUrl = o.value(QStringLiteral("html_url")).toString();
                    bestDownload.clear();
                    bestAsset.clear();
                    // The file for this computer: the Windows zip or the Linux AppImage.
#ifdef Q_OS_WIN
                    const QString want = QStringLiteral("-win64.zip");
#else
                    const QString want = QStringLiteral(".AppImage");
#endif
                    for (const QJsonValue &a : o.value(QStringLiteral("assets")).toArray())
                    {
                        const QJsonObject ao = a.toObject();
                        const QString name = ao.value(QStringLiteral("name")).toString();
                        if (name.endsWith(want, Qt::CaseInsensitive))
                        {
                            bestAsset = name;
                            bestDownload = ao.value(QStringLiteral("browser_download_url")).toString();
                            break;
                        }
                    }
                }
            }
            if (best.empty())
                error = tr("No releases were found.");
        }
    }

    if (!error.isEmpty())
    {
        emit logLine(tr("Update check failed: %1").arg(error));
        if (manual)
            QMessageBox::warning(m_window,
                                 tr("Check for updates"),
                                 tr("The update check did not work:\n\n%1\n\nReleases are listed at\n%2")
                                     .arg(error, QString::fromLatin1(releasesPage)));
        return; // not counted as today's check: try again at the next start
    }

    if (!m_pretend)
        m_settings->setValue(QStringLiteral("Update/LastCheck"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!trustedUrl(bestUrl))
        bestUrl = QString::fromLatin1(releasesPage);
    if (!trustedUrl(bestDownload))
    {
        bestDownload.clear();
        bestAsset.clear();
    }

    const QString newest = QString::fromStdString(versioncheck::toString(best));
    const bool newer = versioncheck::compare(best, versioncheck::parse(m_running.toStdString())) > 0;
    if (!newer)
    {
        emit logLine(tr("Update check: this is the newest version (%1; newest release %2).").arg(m_running, bestTag));
        hideNotice();
        if (manual)
        {
            QMessageBox box(
                QMessageBox::Information,
                tr("Check for updates"),
                tr("You have the newest version.<br><br>Running: %1<br>Newest release: "
                   "<a href=\"%3\">%2</a><br><br>All releases: <a href=\"%4\">%4</a>")
                    .arg(
                        m_running.toHtmlEscaped(), bestTag.toHtmlEscaped(), bestUrl, QString::fromLatin1(releasesPage)),
                QMessageBox::Ok,
                m_window);
            box.setTextFormat(Qt::RichText);
            box.setTextInteractionFlags(Qt::TextBrowserInteraction);
            box.exec();
        }
        return;
    }

    emit logLine(tr("Update check: version %1 is available (%2); running %3.").arg(newest, bestUrl, m_running));
    if (manual)
    {
        QString text = tr("Version %1 is available (you have %2).<br><br>"
                          "Release page (changes and downloads):<br><a href=\"%3\">%3</a>")
                           .arg(newest, m_running.toHtmlEscaped(), bestUrl);
        if (!bestDownload.isEmpty())
            text += tr("<br><br>Download for this computer:<br><a href=\"%1\">%2</a>")
                        .arg(bestDownload, bestAsset.toHtmlEscaped());
        text += tr("<br><br>Nothing is installed automatically: unzip the new version and run it "
                   "in place of this one. Your settings are kept.");
        QMessageBox box(QMessageBox::Information, tr("Check for updates"), text, QMessageBox::NoButton, m_window);
        box.setTextFormat(Qt::RichText);
        box.setTextInteractionFlags(Qt::TextBrowserInteraction);
        QPushButton *download = nullptr;
        if (!bestDownload.isEmpty())
            download = box.addButton(tr("Download"), QMessageBox::AcceptRole);
        QPushButton *open = box.addButton(tr("Open the release page"), QMessageBox::AcceptRole);
        QPushButton *skip = box.addButton(tr("Skip this version"), QMessageBox::DestructiveRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.setDefaultButton(download ? download : open);
        box.exec();
        if (download && box.clickedButton() == download)
        {
            QDesktopServices::openUrl(QUrl(bestDownload));
        }
        else if (box.clickedButton() == open)
        {
            QDesktopServices::openUrl(QUrl(bestUrl));
        }
        else if (box.clickedButton() == skip)
        {
            m_settings->setValue(QStringLiteral("Update/SkipTag"), bestTag);
            hideNotice();
            return;
        }
        showNotice(bestTag, bestUrl);
        return;
    }

    if (!m_pretend && m_settings->value(QStringLiteral("Update/SkipTag")).toString() == bestTag)
        return; // the user said no to this one
    showNotice(bestTag, bestUrl);
}

void UpdateCheck::showNotice(const QString &tag, const QString &url)
{
    m_noticeTag = tag;
    m_noticeUrl = url;
    if (!m_notice)
    {
        m_notice = new QLabel();
        m_notice->setTextFormat(Qt::RichText);
        m_notice->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
        m_notice->setToolTip(tr("A newer release is on GitHub. Nothing is installed automatically."));
        connect(m_notice,
                &QLabel::linkActivated,
                this,
                [this](const QString &link)
                {
                    if (link == QLatin1String("skip"))
                    {
                        m_settings->setValue(QStringLiteral("Update/SkipTag"), m_noticeTag);
                        emit logLine(tr("Update notice for %1 switched off.").arg(m_noticeTag));
                        hideNotice();
                    }
                    else
                    {
                        QDesktopServices::openUrl(QUrl(m_noticeUrl));
                    }
                });
        m_window->statusBar()->addPermanentWidget(m_notice);
    }
    const QString ver = QString::fromStdString(versioncheck::toString(versioncheck::parse(tag.toStdString())));
    m_notice->setText(tr("<a href=\"open\">Version %1 is available</a> &nbsp; <a href=\"skip\">skip</a>").arg(ver));
    m_notice->show();
}

void UpdateCheck::hideNotice()
{
    if (m_notice)
        m_notice->hide();
}
