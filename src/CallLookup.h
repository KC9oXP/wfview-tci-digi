#pragma once

// Callsign lookup for the QSO log: QRZ.com XML (needs an XML subscription for the grid)
// or HamQTH (free account). Off by default.
//
//   lookup(call) --login if needed--> callbook --> found(call, name, qth, grid)
//
// The username is in the settings; the password is in SecretStore ("qrz-xml-password",
// "hamqth-password"), revealed only to build the HTTPS login request. The session key
// lives in memory only. Answers are cached for the session (one request per call), and a
// login that is refused stops lookups until the settings change.

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QSettings;

class CallLookup : public QObject
{
    Q_OBJECT

public:
    enum class Service
    {
        Off = 0,
        Qrz = 1,
        HamQth = 2
    };
    struct Info
    {
        QString name, qth, grid;
    };

    CallLookup(QSettings *settings, QObject *parent = nullptr);

    Service service() const
    {
        return m_service;
    }
    QString serviceName() const;
    QString user() const
    {
        return m_user;
    }
    // Saves the service and username; password: empty keeps the saved one (it is wiped).
    void setConfig(Service s, const QString &user, QString &password);
    void forgetPassword();
    bool hasPassword() const;
    static QString secretName(Service s);
    QString statusText() const
    {
        return m_status;
    }

    void lookup(const QString &call);
    // Logs in and looks up the user's own call: emits testResult.
    void test(const QString &myCall);

    // Tests: send requests here instead of the real callbooks.
    static void setUrlForTests(const QString &url);

signals:
    void found(const QString &call, const CallLookup::Info &info);
    void notFound(const QString &call, const QString &why);
    void testResult(bool pass, const QString &text);
    void statusChanged();

private:
    void login(const std::function<void(bool)> &then);
    void query(const QString &call, bool retried, bool isTest);
    void setStatus(const QString &s);

    QSettings *m_settings;
    QNetworkAccessManager *m_net = nullptr;
    Service m_service = Service::Off;
    QString m_user;
    QString m_session; // in memory only
    bool m_loginRefused = false;
    bool m_loggingIn = false;
    QList<std::function<void(bool)>> m_waiting; // lookups waiting for the login
    QHash<QString, Info> m_cache;
    QHash<QString, bool> m_inFlight;
    QString m_status;
};

Q_DECLARE_METATYPE(CallLookup::Info)
