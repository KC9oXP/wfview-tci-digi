#include "CallLookup.h"

#include "SecretStore.h"
#include "lookupcore.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>
#include <algorithm>
#include <memory>
#include <string>

namespace
{
QString g_testUrl;

QNetworkRequest request(const QString &base, const std::string &query)
{
    QUrl url(base);
    url.setQuery(QString::fromStdString(query), QUrl::StrictMode);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("wfview-tci-digi/" APP_VERSION));
    // Never follow a redirect: the password goes to the callbook and nowhere else.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(20000);
    return req;
}
} // namespace

CallLookup::CallLookup(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    qRegisterMetaType<CallLookup::Info>();
    m_net = new QNetworkAccessManager(this);
    m_service = Service(std::clamp(m_settings->value(QStringLiteral("Lookup/Service"), 0).toInt(), 0, 2));
    m_user = m_settings->value(QStringLiteral("Lookup/User")).toString();
    m_status = m_service == Service::Off ? tr("Callsign lookup is off.") : tr("Ready.");
}

void CallLookup::setUrlForTests(const QString &url)
{
    g_testUrl = url;
}

QString CallLookup::serviceName() const
{
    return m_service == Service::Qrz ? tr("QRZ") : m_service == Service::HamQth ? tr("HamQTH") : tr("off");
}

QString CallLookup::secretName(Service s)
{
    return s == Service::Qrz ? QStringLiteral("qrz-xml-password") : QStringLiteral("hamqth-password");
}

bool CallLookup::hasPassword() const
{
    return m_service != Service::Off && SecretStore::has(secretName(m_service));
}

void CallLookup::setConfig(Service s, const QString &user, QString &password)
{
    m_service = s;
    m_user = user.trimmed();
    m_settings->setValue(QStringLiteral("Lookup/Service"), int(s));
    m_settings->setValue(QStringLiteral("Lookup/User"), m_user);
    if (!password.isEmpty() && s != Service::Off)
        SecretStore::save(secretName(s), password); // wipes it
    wipe(password);
    // New settings: log in afresh and ask again.
    m_session.clear();
    m_loginRefused = false;
    m_cache.clear();
    setStatus(s == Service::Off ? tr("Callsign lookup is off.") : tr("Ready."));
}

void CallLookup::forgetPassword()
{
    if (m_service == Service::Off)
        return;
    SecretStore::forget(secretName(m_service));
    m_session.clear();
    setStatus(tr("Password forgotten."));
}

void CallLookup::setStatus(const QString &s)
{
    m_status = s;
    emit statusChanged();
}

void CallLookup::lookup(const QString &callIn)
{
    if (m_service == Service::Off || m_loginRefused)
        return;
    const QString call = QString::fromStdString(::lookup::baseCall(callIn.trimmed().toStdString()));
    if (call.size() < 3)
        return;
    const auto hit = m_cache.constFind(call);
    if (hit != m_cache.constEnd())
    {
        emit found(callIn.trimmed().toUpper(), hit.value());
        return;
    }
    if (m_inFlight.contains(call))
        return;
    m_inFlight.insert(call, true);
    const QString asked = callIn.trimmed().toUpper();
    if (m_session.isEmpty())
        login(
            [this, call, asked](bool ok)
            {
                if (ok)
                    query(call + QLatin1Char('|') + asked, false, false);
                else
                    m_inFlight.remove(call);
            });
    else
        query(call + QLatin1Char('|') + asked, false, false);
}

void CallLookup::test(const QString &myCall)
{
    if (m_service == Service::Off)
    {
        emit testResult(false, tr("Choose QRZ or HamQTH first."));
        return;
    }
    m_session.clear();
    m_loginRefused = false;
    const QString call = QString::fromStdString(::lookup::baseCall(myCall.trimmed().toStdString()));
    login(
        [this, call](bool ok)
        {
            if (!ok)
            {
                emit testResult(false, m_status);
                return;
            }
            if (call.size() < 3)
            {
                emit testResult(true, tr("Logged in to %1.").arg(serviceName()));
                return;
            }
            query(call + QLatin1Char('|') + call, false, true);
        });
}

void CallLookup::login(const std::function<void(bool)> &then)
{
    m_waiting << then;
    if (m_loggingIn)
        return;
    const auto finish = [this](bool ok)
    {
        m_loggingIn = false;
        const auto waiting = m_waiting;
        m_waiting.clear();
        for (const auto &f : waiting)
            f(ok);
    };
    const std::unique_ptr<SecretBuffer> pw = SecretStore::load(secretName(m_service));
    if (m_user.isEmpty() || !pw || pw->empty())
    {
        setStatus(tr("Enter your %1 username and password (Options > Callsign lookup).").arg(serviceName()));
        m_loginRefused = true;
        finish(false);
        return;
    }
    m_loggingIn = true;
    const Service svc = m_service;
    std::string q;
    pw->reveal(
        [&](const char *p, size_t n)
        {
            std::string pass(p, n);
            q = svc == Service::Qrz
                ? ::lookup::qrzLoginQuery(m_user.toStdString(), pass, "wfview-tci-digi-" APP_VERSION)
                : ::lookup::hamqthLoginQuery(m_user.toStdString(), pass);
            wipe(pass);
        });
    const QString base = !g_testUrl.isEmpty() ? g_testUrl
        : svc == Service::Qrz                 ? QString::fromLatin1(::lookup::kQrzXmlUrl)
                                              : QString::fromLatin1(::lookup::kHamqthUrl);
    QNetworkRequest req = request(base, q);
    wipe(q);
    setStatus(tr("Logging in to %1...").arg(serviceName()));
    QNetworkReply *r = m_net->get(req);
    connect(r,
            &QNetworkReply::finished,
            this,
            [this, r, svc, finish]()
            {
                r->deleteLater();
                if (svc != m_service)
                {
                    finish(false); // settings changed meanwhile
                    return;
                }
                if (r->error() != QNetworkReply::NoError)
                {
                    setStatus(tr("%1 login failed: %2").arg(serviceName(), r->errorString()));
                    finish(false);
                    return;
                }
                const std::string body = r->readAll().toStdString();
                const ::lookup::Result res
                    = svc == Service::Qrz ? ::lookup::parseQrz(body) : ::lookup::parseHamqth(body);
                if (res.session.empty())
                {
                    m_loginRefused = res.loginFailed || !res.error.empty();
                    setStatus(tr("%1 login refused: %2")
                                  .arg(serviceName(),
                                       res.error.empty() ? tr("no session in the answer")
                                                         : QString::fromStdString(res.error)));
                    finish(false);
                    return;
                }
                m_session = QString::fromStdString(res.session);
                QString note = tr("Logged in to %1.").arg(serviceName());
                if (!res.message.empty())
                    note += QLatin1Char(' ') + QString::fromStdString(res.message);
                setStatus(note);
                finish(true);
            });
}

// key: "<base call>|<call as asked>"
void CallLookup::query(const QString &key, bool retried, bool isTest)
{
    const QString call = key.section(QLatin1Char('|'), 0, 0);
    const QString asked = key.section(QLatin1Char('|'), 1);
    const Service svc = m_service;
    const std::string q = svc == Service::Qrz
        ? ::lookup::qrzLookupQuery(m_session.toStdString(), call.toStdString())
        : ::lookup::hamqthLookupQuery(m_session.toStdString(), call.toStdString(), "wfview-tci-digi");
    const QString base = !g_testUrl.isEmpty() ? g_testUrl
        : svc == Service::Qrz                 ? QString::fromLatin1(::lookup::kQrzXmlUrl)
                                              : QString::fromLatin1(::lookup::kHamqthUrl);
    QNetworkReply *r = m_net->get(request(base, q));
    connect(r,
            &QNetworkReply::finished,
            this,
            [this, r, svc, call, asked, key, retried, isTest]()
            {
                r->deleteLater();
                const auto done = [this, &call]() { m_inFlight.remove(call); };
                if (svc != m_service)
                {
                    done();
                    return;
                }
                if (r->error() != QNetworkReply::NoError)
                {
                    setStatus(tr("%1 lookup of %2 failed: %3").arg(serviceName(), call, r->errorString()));
                    if (isTest)
                        emit testResult(false, m_status);
                    done();
                    return;
                }
                const std::string body = r->readAll().toStdString();
                const ::lookup::Result res
                    = svc == Service::Qrz ? ::lookup::parseQrz(body) : ::lookup::parseHamqth(body);
                if (res.sessionBad && !retried)
                {
                    // Session timed out: log in again once and repeat.
                    m_session.clear();
                    login(
                        [this, key, isTest, call](bool ok)
                        {
                            if (ok)
                                query(key, true, isTest);
                            else
                            {
                                m_inFlight.remove(call);
                                if (isTest)
                                    emit testResult(false, m_status);
                            }
                        });
                    return;
                }
                if (!res.session.empty())
                    m_session = QString::fromStdString(res.session);
                if (!res.found)
                {
                    const QString why = res.error.empty() ? tr("not found") : QString::fromStdString(res.error);
                    if (isTest)
                        emit testResult(false, tr("Logged in, but %1: %2").arg(call, why));
                    else
                        emit notFound(asked, why);
                    done();
                    return;
                }
                Info info;
                info.name = QString::fromStdString(res.name);
                info.qth = QString::fromStdString(res.qth);
                info.grid = QString::fromStdString(res.grid);
                m_cache.insert(call, info);
                QString note;
                if (info.grid.isEmpty() && svc == Service::Qrz)
                    note = tr(" (no grid: QRZ gives the grid only with an XML subscription)");
                setStatus(tr("%1: %2 found.%3").arg(serviceName(), call, note));
                if (isTest)
                    emit testResult(true,
                                    tr("Pass: %1 found: %2, %3, %4%5")
                                        .arg(call,
                                             info.name.isEmpty() ? tr("(no name)") : info.name,
                                             info.qth.isEmpty() ? tr("(no QTH)") : info.qth,
                                             info.grid.isEmpty() ? tr("(no grid)") : info.grid,
                                             note));
                else
                    emit found(asked, info);
                done();
            });
}
