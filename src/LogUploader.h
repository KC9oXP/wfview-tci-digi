#pragma once

// Where a logged QSO goes besides the ADIF file (which is always written):
//  - Another program: the ADIF record as one UDP datagram (GridTracker2 "Receive ADIF UDP
//    Broadcasts For Logging", default 127.0.0.1:2333; Log4OM ...). Nothing secret involved.
//  - QRZ.com logbook: INSERT with the logbook API key (SecretStore "qrz-key").
//  - LoTW: TQSL signs and uploads, hidden, with the certificate password (SecretStore
//    "tqsl-password"; none if the certificate has no password).
// Each QSO is sent as soon as it is logged. A QSO that cannot be sent stays queued
// (upload-pending-*.adi in the logs folder, QSO data only) and goes with the next one.
// A login problem stops that service until its settings change or Retry is pressed.

#include "QsoLog.h"

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <memory>

class QNetworkAccessManager;
class QSettings;
class QWidget;

class LogUploader : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Off,
        Ok, // nothing waiting, last upload fine
        Busy, // sending
        Waiting, // queued, will retry (network)
        Problem // login or setup problem: stopped
    };

    explicit LogUploader(QSettings *settings, QObject *parent = nullptr);
    ~LogUploader() override;

    bool udpEnabled() const
    {
        return m_udpOn;
    }
    bool qrzEnabled() const
    {
        return m_qrzOn;
    }
    bool lotwEnabled() const
    {
        return m_lotwOn;
    }
    State qrzState() const;
    State lotwState() const;
    QString qrzText() const;
    QString lotwText() const;
    int qrzWaiting() const
    {
        return m_qrzQueue.size();
    }
    int lotwWaiting() const
    {
        return m_lotwQueue.size();
    }

    // TQSL found automatically (registry / usual places), else the one the user chose.
    QString tqslPath(bool *fromSystem = nullptr) const;
    // The Station Location names set up in TQSL (its station_data file); empty if it can't be read.
    QStringList tqslStations() const;

    // Options > Logging and uploads...
    void editSettings(QWidget *parent);

    // For the end-to-end test only.
    static void setQrzUrlForTests(const QUrl &url);
    static void setTqslPathForTests(const QString &path);
    static void setTqslStationFileForTests(const QString &path);

public slots:
    void qsoLogged(const Qso &q);
    void retryNow();
    // QRZ STATUS with the saved key: emits qrzTestResult.
    void testQrz();

signals:
    void statusChanged();
    void message(const QString &line); // TCI log (never contains a secret)
    void qrzTestResult(bool pass, const QString &text);

private:
    void load();
    void saveSettings();
    void saveQueue(const QString &which);
    QString queuePath(const QString &which) const;
    void process();
    void sendQrz();
    void sendLotw();
    void sendUdp(const Qso &q);
    QUrl qrzUrl() const;

    QSettings *m_settings;
    bool m_udpOn = false;
    QString m_udpHost = QStringLiteral("127.0.0.1");
    quint16 m_udpPort = 2333;
    bool m_qrzOn = false;
    bool m_lotwOn = false;
    QString m_tqslUserPath;
    QString m_location;

    QList<Qso> m_qrzQueue, m_lotwQueue;
    bool m_qrzBusy = false, m_lotwBusy = false;
    bool m_qrzStopped = false, m_lotwStopped = false;
    bool m_qrzRetry = false, m_lotwRetry = false;
    QString m_qrzMsg, m_lotwMsg;
    int m_qrzSent = 0, m_lotwSent = 0;

    QUdpSocket m_udp;
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QProcess> m_tqsl;
    std::unique_ptr<QTemporaryDir> m_tqslDir; // temporary folder of the running upload
    QTimer m_retry; // network trouble: try again in a few minutes
};
