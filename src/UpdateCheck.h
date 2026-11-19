#pragma once

// Looks on GitHub for a newer release of the program and tells the user.
// Nothing is downloaded or installed: the notice links to the release page.
//
// - At every start-up (Options > Check for updates at start-up),
//   or at any time from Help > Check for updates.
// - The only thing sent is the request for the public release list.
// - "Skip this version" silences the notice until a newer release appears.

#include <QObject>
#include <QString>

class QLabel;
class QMainWindow;
class QNetworkAccessManager;
class QNetworkReply;
class QSettings;

class UpdateCheck : public QObject
{
    Q_OBJECT

public:
    UpdateCheck(QSettings *settings, QMainWindow *window);

    bool automatic() const;
    void setAutomatic(bool on);

    // The version this program compares itself with (APP_VERSION, or --pretend-version for testing).
    QString runningVersion() const
    {
        return m_running;
    }

    // Start-up check: does nothing when switched off or in a build without a version number.
    void startAutomatic();

public slots:
    // Help > Check for updates: always asks, and always reports the result in a box.
    void checkNow();

signals:
    void logLine(const QString &line);

private:
    void request(bool manual);
    void finished(QNetworkReply *reply, bool manual);
    void showNotice(const QString &tag, const QString &url);
    void hideNotice();

    QSettings *m_settings;
    QMainWindow *m_window;
    QNetworkAccessManager *m_net = nullptr;
    QLabel *m_notice = nullptr;
    QString m_running;
    bool m_pretend = false;
    bool m_busy = false;
    QString m_noticeTag;
    QString m_noticeUrl;
};
