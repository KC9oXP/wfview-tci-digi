#include "LogUploader.h"

#include "AppPaths.h"
#include "SecretStore.h"
#include "callspot.h"
#include "uploadcore.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace
{
const QString kQrzKey = QStringLiteral("qrz-key");
const QString kTqslPassword = QStringLiteral("tqsl-password");
QUrl g_qrzUrlForTests;
QString g_tqslForTests;
constexpr int kRetryMs = 5 * 60 * 1000; // network trouble: try again after this
constexpr int kTqslTimeoutMs = 180 * 1000;
constexpr int kMaxPerTqslRun = 50;

QString lastLine(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i)
        if (!lines.at(i).trimmed().isEmpty())
            return lines.at(i).trimmed();
    return QString();
}
} // namespace

void LogUploader::setQrzUrlForTests(const QUrl &url)
{
    g_qrzUrlForTests = url;
}

void LogUploader::setTqslPathForTests(const QString &path)
{
    g_tqslForTests = path;
}

LogUploader::LogUploader(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_net = new QNetworkAccessManager(this);
    m_retry.setSingleShot(true);
    m_retry.setInterval(kRetryMs);
    connect(&m_retry,
            &QTimer::timeout,
            this,
            [this]()
            {
                m_qrzRetry = m_lotwRetry = false;
                process();
            });
    load();
}

LogUploader::~LogUploader()
{
    if (m_tqsl)
        m_tqsl->kill();
}

QUrl LogUploader::qrzUrl() const
{
    // Built in: a settings file cannot send the key anywhere else.
    return g_qrzUrlForTests.isValid() ? g_qrzUrlForTests : QUrl(QString::fromLatin1(upload::kQrzUrl));
}

QString LogUploader::queuePath(const QString &which) const
{
    return QDir(AppPaths::logs()).filePath(QStringLiteral("upload-pending-%1.adi").arg(which));
}

void LogUploader::load()
{
    m_settings->beginGroup(QStringLiteral("Upload"));
    m_udpOn = m_settings->value(QStringLiteral("UdpEnabled"), false).toBool();
    m_udpHost = m_settings->value(QStringLiteral("UdpHost"), QStringLiteral("127.0.0.1")).toString();
    m_udpPort = quint16(m_settings->value(QStringLiteral("UdpPort"), 2333).toUInt());
    m_qrzOn = m_settings->value(QStringLiteral("QrzEnabled"), false).toBool();
    m_lotwOn = m_settings->value(QStringLiteral("LotwEnabled"), false).toBool();
    m_tqslUserPath = m_settings->value(QStringLiteral("TqslPath")).toString();
    m_location = m_settings->value(QStringLiteral("LotwLocation")).toString();
    m_settings->endGroup();
    for (const QString &which : {QStringLiteral("qrz"), QStringLiteral("lotw")})
    {
        QFile f(queuePath(which));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QList<Qso> q = Adif::parse(QString::fromUtf8(f.readAll()));
        (which == QLatin1String("qrz") ? m_qrzQueue : m_lotwQueue) = q;
    }
}

void LogUploader::saveSettings()
{
    m_settings->beginGroup(QStringLiteral("Upload"));
    m_settings->setValue(QStringLiteral("UdpEnabled"), m_udpOn);
    m_settings->setValue(QStringLiteral("UdpHost"), m_udpHost);
    m_settings->setValue(QStringLiteral("UdpPort"), m_udpPort);
    m_settings->setValue(QStringLiteral("QrzEnabled"), m_qrzOn);
    m_settings->setValue(QStringLiteral("LotwEnabled"), m_lotwOn);
    m_settings->setValue(QStringLiteral("TqslPath"), m_tqslUserPath);
    m_settings->setValue(QStringLiteral("LotwLocation"), m_location);
    m_settings->endGroup();
}

void LogUploader::saveQueue(const QString &which)
{
    const QList<Qso> &q = which == QLatin1String("qrz") ? m_qrzQueue : m_lotwQueue;
    const QString path = queuePath(which);
    if (q.isEmpty())
    {
        QFile::remove(path);
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(Adif::header().toUtf8());
    for (const Qso &x : q)
        f.write(Adif::record(x).toUtf8());
}

QString LogUploader::tqslPath(bool *fromSystem) const
{
    if (fromSystem)
        *fromSystem = true;
    if (!g_tqslForTests.isEmpty())
        return g_tqslForTests;
    QStringList candidates;
#ifdef Q_OS_WIN
    // Where the TQSL installer says it is (HKLM: only an administrator can change it).
    for (const QSettings::Format fmt : {QSettings::Registry64Format, QSettings::Registry32Format})
    {
        QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\TrustedQSL"), fmt);
        const QString ip = reg.value(QStringLiteral("InstallPath")).toString();
        if (ip.isEmpty())
            continue;
        const QFileInfo fi(ip);
        candidates << (fi.isDir() ? QDir(ip).filePath(QStringLiteral("tqsl.exe")) : ip);
    }
    candidates << QStringLiteral("C:/Program Files (x86)/TrustedQSL/tqsl.exe")
               << QStringLiteral("C:/Program Files/TrustedQSL/tqsl.exe");
#elif defined(Q_OS_MACOS)
    candidates << QStringLiteral("/Applications/TrustedQSL/tqsl.app/Contents/MacOS/tqsl")
               << QStringLiteral("/Applications/tqsl.app/Contents/MacOS/tqsl");
#else
    candidates << QStringLiteral("/usr/bin/tqsl") << QStringLiteral("/usr/local/bin/tqsl");
#endif
    for (const QString &c : candidates)
        if (QFileInfo(c).isFile())
            return QDir::toNativeSeparators(c);
    // Only if TQSL is not where it is normally installed: the one the user picked.
    const QFileInfo user(m_tqslUserPath);
    if (user.isFile() && user.baseName().compare(QLatin1String("tqsl"), Qt::CaseInsensitive) == 0)
    {
        if (fromSystem)
            *fromSystem = false;
        return QDir::toNativeSeparators(user.absoluteFilePath());
    }
    return QString();
}

LogUploader::State LogUploader::qrzState() const
{
    if (!m_qrzOn)
        return State::Off;
    if (m_qrzStopped)
        return State::Problem;
    if (m_qrzBusy)
        return State::Busy;
    if (!m_qrzQueue.isEmpty())
        return State::Waiting;
    return State::Ok;
}

LogUploader::State LogUploader::lotwState() const
{
    if (!m_lotwOn)
        return State::Off;
    if (m_lotwStopped)
        return State::Problem;
    if (m_lotwBusy)
        return State::Busy;
    if (!m_lotwQueue.isEmpty())
        return State::Waiting;
    return State::Ok;
}

QString LogUploader::qrzText() const
{
    if (!m_qrzOn)
        return tr("QRZ upload off");
    QString t = tr("QRZ: %1 sent this session, %2 waiting.").arg(m_qrzSent).arg(m_qrzQueue.size());
    if (!m_qrzMsg.isEmpty())
        t += QLatin1Char(' ') + m_qrzMsg;
    return t;
}

QString LogUploader::lotwText() const
{
    if (!m_lotwOn)
        return tr("LoTW upload off");
    QString t = tr("LoTW: %1 sent this session, %2 waiting.").arg(m_lotwSent).arg(m_lotwQueue.size());
    if (!m_lotwMsg.isEmpty())
        t += QLatin1Char(' ') + m_lotwMsg;
    return t;
}

void LogUploader::qsoLogged(const Qso &q)
{
    if (!callspot::isCallsign(q.call.toStdString()))
    {
        if (m_udpOn || m_qrzOn || m_lotwOn)
            emit message(
                tr("Upload: \"%1\" does not look like a call sign; only written to the ADIF file.").arg(q.call));
        return;
    }
    if (m_udpOn)
        sendUdp(q);
    bool queued = false;
    if (m_qrzOn)
    {
        m_qrzQueue << q;
        saveQueue(QStringLiteral("qrz"));
        queued = true;
    }
    if (m_lotwOn)
    {
        m_lotwQueue << q;
        saveQueue(QStringLiteral("lotw"));
        queued = true;
    }
    if (queued)
    {
        // A new QSO also retries what is waiting (network trouble), as GridTracker does.
        m_qrzRetry = m_lotwRetry = false;
        process();
    }
}

void LogUploader::retryNow()
{
    m_qrzStopped = m_lotwStopped = false;
    m_qrzRetry = m_lotwRetry = false;
    m_qrzMsg.clear();
    m_lotwMsg.clear();
    process();
    emit statusChanged();
}

void LogUploader::process()
{
    sendQrz();
    sendLotw();
    emit statusChanged();
}

void LogUploader::sendUdp(const Qso &q)
{
    const QHostAddress to(m_udpHost);
    if (to.isNull())
    {
        emit message(tr("Upload: \"%1\" is not an IP address (Options > Logging and uploads).").arg(m_udpHost));
        return;
    }
    const QByteArray d = Adif::record(q).toUtf8();
    if (m_udp.writeDatagram(d, to, m_udpPort) == d.size())
        emit message(tr("Logged %1 sent to %2:%3 (ADIF over UDP)").arg(q.call).arg(m_udpHost).arg(m_udpPort));
    else
        emit message(
            tr("Upload: ADIF over UDP to %1:%2 failed: %3").arg(m_udpHost).arg(m_udpPort).arg(m_udp.errorString()));
}

// ---------------------------------------------------------------------------
// QRZ

static QNetworkRequest qrzRequest(const QUrl &url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("wfview-tci-digi/" APP_VERSION));
    // Never follow a redirect: the key goes to the logbook address and nowhere else.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(30000);
    return req;
}

// Body for a QRZ request with the saved key; the key is only in clear inside this.
static QByteArray qrzBody(const SecretBuffer &key, const char *action, const QString &adif)
{
    QByteArray body;
    key.reveal(
        [&](const char *p, size_t n)
        {
            std::string k(p, n);
            std::vector<std::pair<std::string, std::string>> fields = {{"KEY", k}, {"ACTION", action}};
            if (!adif.isEmpty())
                fields.push_back({"ADIF", adif.toStdString()});
            std::string b = upload::formBody(fields);
            wipe(fields[0].second);
            body = QByteArray(b.data(), int(b.size()));
            wipe(k);
            wipe(b);
        });
    return body;
}

void LogUploader::sendQrz()
{
    if (!m_qrzOn || m_qrzBusy || m_qrzStopped || m_qrzRetry || m_qrzQueue.isEmpty())
        return;
    const std::unique_ptr<SecretBuffer> key = SecretStore::load(kQrzKey);
    if (!key || key->empty())
    {
        m_qrzStopped = true;
        m_qrzMsg = tr("No API key saved (Options > Logging and uploads).");
        emit statusChanged();
        return;
    }
    const Qso q = m_qrzQueue.first();
    const QByteArray body = qrzBody(*key, "INSERT", Adif::record(q).trimmed());
    m_qrzBusy = true;
    QNetworkReply *r = m_net->post(qrzRequest(qrzUrl()), body);
    connect(r,
            &QNetworkReply::finished,
            this,
            [this, r, body, call = q.call]()
            {
                wipeShared(body); // the network copy too: the request is done
                r->deleteLater();
                m_qrzBusy = false;
                const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                upload::Outcome o = upload::Outcome::Retry;
                QString why;
                if (r->error() != QNetworkReply::NoError)
                    why = tr("Cannot reach QRZ: %1").arg(r->errorString());
                else if (http >= 300 && http < 400)
                    why = tr("QRZ answered with a redirect (not followed).");
                else
                {
                    const upload::QrzReply rep = upload::parseQrz(r->readAll().toStdString());
                    o = upload::qrzInsertOutcome(rep);
                    if (rep.result == "AUTH")
                        why = tr("QRZ refused the key for uploads (subscription?).");
                    else if (o == upload::Outcome::Stop)
                        why = tr("QRZ: %1").arg(QString::fromStdString(rep.reason));
                    else if (o == upload::Outcome::Retry)
                        why = tr("Unexpected answer from QRZ.");
                }
                if (o == upload::Outcome::Done)
                {
                    if (!m_qrzQueue.isEmpty())
                        m_qrzQueue.removeFirst();
                    saveQueue(QStringLiteral("qrz"));
                    ++m_qrzSent;
                    m_qrzMsg.clear();
                    emit message(tr("QRZ: %1 uploaded").arg(call));
                }
                else if (o == upload::Outcome::Retry)
                {
                    m_qrzRetry = true;
                    m_qrzMsg = why + tr(" Will try again.");
                    m_retry.start();
                    emit message(m_qrzMsg);
                }
                else
                {
                    m_qrzStopped = true;
                    m_qrzMsg = why;
                    emit message(tr("QRZ upload stopped: %1").arg(why));
                }
                process();
            });
}

void LogUploader::testQrz()
{
    const std::unique_ptr<SecretBuffer> key = SecretStore::load(kQrzKey);
    if (!key || key->empty())
    {
        emit qrzTestResult(false, tr("Fail: no API key saved."));
        return;
    }
    const QByteArray body = qrzBody(*key, "STATUS", QString());
    QNetworkReply *r = m_net->post(qrzRequest(qrzUrl()), body);
    connect(r,
            &QNetworkReply::finished,
            this,
            [this, r, body]()
            {
                wipeShared(body);
                r->deleteLater();
                const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (r->error() != QNetworkReply::NoError)
                {
                    emit qrzTestResult(false, tr("Fail: cannot reach QRZ (%1).").arg(r->errorString()));
                    return;
                }
                if (http >= 300 && http < 400)
                {
                    emit qrzTestResult(false, tr("Fail: QRZ answered with a redirect (not followed)."));
                    return;
                }
                const upload::QrzReply rep = upload::parseQrz(r->readAll().toStdString());
                const auto v = [&](const char *k)
                {
                    auto it = rep.values.find(k);
                    return it == rep.values.end() ? QString() : QString::fromStdString(it->second);
                };
                if (rep.result == "OK")
                {
                    QString who = v("CALLSIGN");
                    if (who.isEmpty())
                        who = v("OWNER");
                    if (who.isEmpty())
                        who = v("BOOK_NAME");
                    QString t = tr("Pass: key accepted");
                    if (!who.isEmpty())
                        t += tr(" (logbook %1)").arg(who);
                    if (!v("COUNT").isEmpty())
                        t += tr(", %1 QSOs").arg(v("COUNT"));
                    emit qrzTestResult(true, t + QLatin1Char('.'));
                    // A good key: let a stopped upload go again.
                    if (m_qrzStopped)
                    {
                        m_qrzStopped = false;
                        m_qrzMsg.clear();
                        process();
                    }
                }
                else if (rep.result == "AUTH")
                    emit qrzTestResult(false, tr("Fail: key known, but QRZ does not allow this (subscription?)."));
                else if (rep.result == "FAIL")
                    emit qrzTestResult(false, tr("Fail: QRZ says: %1").arg(QString::fromStdString(rep.reason)));
                else
                    emit qrzTestResult(false, tr("Fail: unexpected answer from QRZ."));
            });
}

// ---------------------------------------------------------------------------
// LoTW (TQSL)

void LogUploader::sendLotw()
{
    if (!m_lotwOn || m_lotwBusy || m_lotwStopped || m_lotwRetry || m_lotwQueue.isEmpty())
        return;
    bool system = true;
    const QString exe = tqslPath(&system);
    if (exe.isEmpty())
    {
        m_lotwStopped = true;
        m_lotwMsg = tr("TQSL not found. Install TQSL, or pick tqsl.exe in Options > Logging and uploads.");
        emit statusChanged();
        return;
    }
    if (m_location.trimmed().isEmpty())
    {
        m_lotwStopped = true;
        m_lotwMsg = tr("Set your TQSL station location (Options > Logging and uploads).");
        emit statusChanged();
        return;
    }
    // One file with what is waiting (normally just the QSO just logged).
    m_tqslDir = std::make_unique<QTemporaryDir>();
    if (!m_tqslDir->isValid())
    {
        m_lotwRetry = true;
        m_lotwMsg = tr("Could not make a temporary folder.");
        m_retry.start();
        return;
    }
    const int n = std::min<int>(int(m_lotwQueue.size()), kMaxPerTqslRun);
    const QString adi = m_tqslDir->filePath(QStringLiteral("upload.adi"));
    {
        QFile f(adi);
        if (!f.open(QIODevice::WriteOnly))
        {
            m_lotwRetry = true;
            m_lotwMsg = tr("Could not write the file for TQSL.");
            m_retry.start();
            return;
        }
        f.write(Adif::header().toUtf8());
        for (int i = 0; i < n; ++i)
            f.write(Adif::record(m_lotwQueue.at(i)).toUtf8());
    }

    std::string pw;
    if (const std::unique_ptr<SecretBuffer> saved = SecretStore::load(kTqslPassword))
        saved->reveal([&](const char *p, size_t len) { pw.assign(p, len); });
    std::vector<std::string> args
        = upload::tqslArgs(m_location.toStdString(), pw, QDir::toNativeSeparators(adi).toStdString());
    const QString shown = QString::fromStdString(upload::tqslArgsShown(args));
    QStringList qargs;
    for (std::string &a : args)
    {
        qargs << QString::fromStdString(a);
        wipe(a);
    }
    wipe(pw);

    auto *p = new QProcess(this);
    p->setProgram(exe); // straight to TQSL: no shell, no search of PATH or the current folder
    p->setArguments(qargs);
    p->setWorkingDirectory(m_tqslDir->path());
#ifdef Q_OS_WIN
    p->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *a)
        {
            a->flags |= CREATE_NO_WINDOW;
            a->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
            a->startupInfo->wShowWindow = SW_HIDE;
        });
#endif
    m_tqsl = p;
    m_lotwBusy = true;
    emit message(tr("LoTW: %1 %2%3").arg(exe, shown, system ? QString() : tr(" (TQSL picked by you)")));
    QTimer::singleShot(kTqslTimeoutMs,
                       p,
                       [p]()
                       {
                           if (p->state() != QProcess::NotRunning)
                               p->kill();
                       });
    connect(p,
            &QProcess::finished,
            this,
            [this, p, n](int exitCode, QProcess::ExitStatus es)
            {
                const QString err = QString::fromLocal8Bit(p->readAllStandardError());
                p->deleteLater();
                m_tqslDir.reset(); // deletes the temporary file
                m_lotwBusy = false;
                int status = upload::tqslStatus(exitCode, err.toStdString());
                if (es == QProcess::CrashExit)
                    status = 4;
                const upload::Outcome o = upload::tqslOutcome(status);
                const QString text = QString::fromStdString(upload::tqslStatusText(status));
                if (o == upload::Outcome::Done)
                {
                    const int k = std::min<int>(n, int(m_lotwQueue.size()));
                    m_lotwQueue.erase(m_lotwQueue.begin(), m_lotwQueue.begin() + k);
                    saveQueue(QStringLiteral("lotw"));
                    m_lotwSent += k;
                    m_lotwMsg.clear();
                    emit message(tr("LoTW: %n QSO(s) %1", nullptr, k).arg(text));
                }
                else if (o == upload::Outcome::Retry)
                {
                    m_lotwRetry = true;
                    m_lotwMsg = tr("LoTW: %1. Will try again.").arg(text);
                    m_retry.start();
                    emit message(m_lotwMsg);
                }
                else
                {
                    m_lotwStopped = true;
                    QString detail = lastLine(err);
                    if (detail.contains(QLatin1String("-p"))) // never show a command line back
                        detail.clear();
                    m_lotwMsg = text + (detail.isEmpty() ? QString() : QStringLiteral(": ") + detail);
                    emit message(tr("LoTW upload stopped: %1").arg(m_lotwMsg));
                }
                process();
            });
    connect(p,
            &QProcess::errorOccurred,
            this,
            [this, p](QProcess::ProcessError e)
            {
                if (e != QProcess::FailedToStart)
                    return;
                p->deleteLater();
                m_tqslDir.reset();
                m_lotwBusy = false;
                m_lotwStopped = true;
                m_lotwMsg = tr("TQSL could not be started.");
                emit message(m_lotwMsg);
                emit statusChanged();
            });
    p->start();
    for (QString &a : qargs)
        wipe(a); // QProcess has its own copy now
}

// ---------------------------------------------------------------------------
// Options > Logging and uploads...

namespace
{
// A saved secret shows as stars only, with Change and Forget; never its value.
struct SecretRow
{
    QWidget *widget = nullptr;
    QLineEdit *edit = nullptr;
    QPushButton *change = nullptr, *forget = nullptr, *save = nullptr, *cancel = nullptr;
    QLabel *note = nullptr;
};

SecretRow makeSecretRow(const QString &name, const QString &placeholder, QWidget *parent)
{
    SecretRow r;
    r.widget = new QWidget(parent);
    auto *h = new QHBoxLayout(r.widget);
    h->setContentsMargins(0, 0, 0, 0);
    r.edit = new QLineEdit();
    r.edit->setMinimumWidth(220);
    r.change = new QPushButton(QObject::tr("Change"));
    r.forget = new QPushButton(QObject::tr("Forget"));
    r.save = new QPushButton(QObject::tr("Save"));
    r.cancel = new QPushButton(QObject::tr("Cancel"));
    r.note = new QLabel();
    h->addWidget(r.edit, 1);
    h->addWidget(r.change);
    h->addWidget(r.forget);
    h->addWidget(r.save);
    h->addWidget(r.cancel);
    h->addWidget(r.note);
    const SecretRow rr = r;
    const auto showSaved = [rr, placeholder](bool saved)
    {
        rr.edit->clear();
        if (saved)
        {
            rr.edit->setEchoMode(QLineEdit::Normal);
            rr.edit->setText(
                QStringLiteral("\u2022\u2022\u2022\u2022\u2022\u2022\u2022\u2022")); // stars, not the secret
            rr.edit->setReadOnly(true);
            rr.edit->setPlaceholderText(QString());
        }
        else
        {
            rr.edit->setEchoMode(QLineEdit::Password);
            rr.edit->setReadOnly(false);
            rr.edit->setPlaceholderText(placeholder);
        }
        rr.change->setVisible(saved);
        rr.forget->setVisible(saved);
        rr.save->setVisible(!saved);
        rr.cancel->setVisible(false);
        rr.note->setText(
            saved ? (SecretStore::persistent() ? QObject::tr("saved") : QObject::tr("kept until the app closes"))
                  : QString());
    };
    showSaved(SecretStore::has(name));
    QObject::connect(r.change,
                     &QPushButton::clicked,
                     r.widget,
                     [rr, showSaved]()
                     {
                         showSaved(false);
                         rr.cancel->setVisible(true);
                         rr.edit->setFocus();
                     });
    QObject::connect(
        r.cancel, &QPushButton::clicked, r.widget, [showSaved, name]() { showSaved(SecretStore::has(name)); });
    QObject::connect(r.forget,
                     &QPushButton::clicked,
                     r.widget,
                     [showSaved, name]()
                     {
                         SecretStore::forget(name);
                         showSaved(false);
                     });
    const auto doSave = [rr, showSaved, name]()
    {
        QString v = rr.edit->text();
        rr.edit->clear();
        if (v.trimmed().isEmpty())
            return;
        if (!SecretStore::save(name, v)) // wipes v
            rr.note->setText(QObject::tr("could not save"));
        else
            showSaved(true);
    };
    QObject::connect(r.save, &QPushButton::clicked, r.widget, doSave);
    QObject::connect(r.edit,
                     &QLineEdit::returnPressed,
                     r.widget,
                     [rr, doSave]()
                     {
                         if (!rr.edit->isReadOnly())
                             doSave();
                     });
    return r;
}

QLabel *resultLabel()
{
    auto *l = new QLabel();
    l->setWordWrap(true);
    return l;
}

void showResult(QLabel *l, bool pass, const QString &text)
{
    l->setText(text);
    l->setStyleSheet(pass ? QStringLiteral("QLabel { color: white; background: #2e8b3a; padding: 2px 6px; "
                                           "border-radius: 3px; font-weight: bold; }")
                          : QStringLiteral("QLabel { color: white; background: #b03030; padding: 2px 6px; "
                                           "border-radius: 3px; font-weight: bold; }"));
}
} // namespace

void LogUploader::editSettings(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Logging and uploads"));
    auto *v = new QVBoxLayout(&dlg);
    auto *intro
        = new QLabel(tr("Every logged QSO is always written to the ADIF file in the logs folder. Each QSO can "
                        "also go, as soon as it is logged, to another program and/or straight to QRZ and LoTW."));
    intro->setWordWrap(true);
    v->addWidget(intro);

    // Another program
    auto *udpBox = new QGroupBox(tr("Send to another program (ADIF over UDP)"));
    udpBox->setCheckable(true);
    udpBox->setChecked(m_udpOn);
    auto *uf = new QFormLayout(udpBox);
    auto *udpHost = new QLineEdit(m_udpHost);
    udpHost->setToolTip(tr("IP address of the PC running the program; 127.0.0.1 for this PC."));
    auto *udpPort = new QSpinBox();
    udpPort->setRange(1, 65535);
    udpPort->setValue(m_udpPort);
    uf->addRow(tr("Address"), udpHost);
    uf->addRow(tr("Port"), udpPort);
    auto *udpNote = new QLabel(tr("GridTracker2: Settings > General > Receive ADIF UDP Broadcasts For Logging, "
                                  "port 2333, Enabled. It then uploads with its own LoTW / QRZ settings."));
    udpNote->setWordWrap(true);
    uf->addRow(udpNote);
    v->addWidget(udpBox);

    // QRZ
    auto *qrzBox = new QGroupBox(tr("Upload to my QRZ.com logbook"));
    qrzBox->setCheckable(true);
    qrzBox->setChecked(m_qrzOn);
    auto *qf = new QFormLayout(qrzBox);
    SecretRow key = makeSecretRow(kQrzKey, tr("paste the API key from your QRZ logbook settings"), &dlg);
    qf->addRow(tr("Logbook API key"), key.widget);
    auto *testRow = new QHBoxLayout();
    auto *test = new QPushButton(tr("Test key"));
    auto *testResult = resultLabel();
    testRow->addWidget(test);
    testRow->addWidget(testResult, 1);
    qf->addRow(testRow);
    auto *qrzNote = new QLabel(tr("The key is the one under Settings in your QRZ logbook (not your QRZ password). "
                                  "Uploading needs a QRZ subscription."));
    qrzNote->setWordWrap(true);
    qf->addRow(qrzNote);
    v->addWidget(qrzBox);
    connect(test,
            &QPushButton::clicked,
            &dlg,
            [this, testResult, test]()
            {
                testResult->setStyleSheet(QString());
                testResult->setText(tr("Testing..."));
                test->setEnabled(false);
                testQrz();
            });
    const QMetaObject::Connection testConn = connect(this,
                                                     &LogUploader::qrzTestResult,
                                                     &dlg,
                                                     [testResult, test](bool pass, const QString &t)
                                                     {
                                                         test->setEnabled(true);
                                                         showResult(testResult, pass, t);
                                                     });

    // LoTW
    auto *lotwBox = new QGroupBox(tr("Upload to LoTW (signed by TQSL)"));
    lotwBox->setCheckable(true);
    lotwBox->setChecked(m_lotwOn);
    auto *lf = new QFormLayout(lotwBox);
    auto *tqslRow = new QHBoxLayout();
    auto *tqslLabel = new QLabel();
    tqslLabel->setWordWrap(true);
    auto *browse = new QPushButton(tr("Pick tqsl..."));
    tqslRow->addWidget(tqslLabel, 1);
    tqslRow->addWidget(browse);
    QString userPath = m_tqslUserPath;
    const auto showTqsl = [this, tqslLabel, browse, &userPath]()
    {
        const QString keep = m_tqslUserPath;
        m_tqslUserPath = userPath;
        bool system = true;
        const QString p = tqslPath(&system);
        m_tqslUserPath = keep;
        tqslLabel->setText(p.isEmpty()  ? tr("<b>TQSL not found</b>")
                               : system ? tr("Found: %1").arg(p)
                                        : tr("Picked by you: %1").arg(p));
        browse->setVisible(p.isEmpty() || !system);
    };
    showTqsl();
    connect(browse,
            &QPushButton::clicked,
            &dlg,
            [&dlg, &userPath, showTqsl]()
            {
                const QString f = QFileDialog::getOpenFileName(
                    &dlg, tr("Where is TQSL?"), QString(), QStringLiteral("tqsl (tqsl tqsl.exe)"));
                if (!f.isEmpty())
                    userPath = f;
                showTqsl();
            });
    lf->addRow(tr("TQSL"), tqslRow);
    auto *loc = new QLineEdit(m_location);
    loc->setPlaceholderText(tr("the Station Location name as set up in TQSL"));
    lf->addRow(tr("Station location"), loc);
    SecretRow pw = makeSecretRow(kTqslPassword, tr("leave empty if your certificate has no password"), &dlg);
    lf->addRow(tr("Certificate password"), pw.widget);
    auto *lotwNote = new QLabel(tr("TQSL signs and uploads each QSO in the background: no TQSL window and no "
                                   "password to type once it is saved here."));
    lotwNote->setWordWrap(true);
    lf->addRow(lotwNote);
    v->addWidget(lotwBox);

    auto *storeNote
        = new QLabel(SecretStore::persistent()
                         ? tr("The key and password are kept in Windows Credential Manager (encrypted under your "
                              "Windows login), never in a file. Shown only as stars.")
                         : tr("This computer has no supported password store yet: the key and password are kept "
                              "only until the app closes."));
    storeNote->setWordWrap(true);
    v->addWidget(storeNote);

    auto *status = new QLabel();
    status->setWordWrap(true);
    const auto showStatus = [this, status]() { status->setText(qrzText() + QLatin1Char('\n') + lotwText()); };
    showStatus();
    const QMetaObject::Connection stConn = connect(this, &LogUploader::statusChanged, &dlg, showStatus);
    auto *retry = new QPushButton(tr("Retry now"));
    retry->setToolTip(tr("Send what is waiting now (and after a problem is fixed)."));
    auto *sr = new QHBoxLayout();
    sr->addWidget(status, 1);
    sr->addWidget(retry);
    v->addLayout(sr);
    connect(retry, &QPushButton::clicked, this, &LogUploader::retryNow);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    const int res = dlg.exec();
    disconnect(testConn);
    disconnect(stConn);
    if (res != QDialog::Accepted)
        return;
    m_udpOn = udpBox->isChecked();
    m_udpHost = udpHost->text().trimmed();
    m_udpPort = quint16(udpPort->value());
    m_qrzOn = qrzBox->isChecked();
    m_lotwOn = lotwBox->isChecked();
    m_tqslUserPath = userPath;
    m_location = loc->text().trimmed();
    saveSettings();
    retryNow(); // new settings: try what is waiting
}
