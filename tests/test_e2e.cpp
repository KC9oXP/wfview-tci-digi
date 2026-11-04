// End-to-end test of the TCI client, the RTTY and PSK engines, Feld Hell receive and the CW decoder
// against MockTciServer (which behaves like wfview 3.00's TCI server).
//
//   test_e2e            run the tests (about 40 s, real time)
//   test_e2e --serve N  only run the mock server on port N (to try the app without a radio)
//   test_e2e --screens DIR  also render the main window at several sizes to PNGs in DIR

#include "CwDecoder.h"
#include "HellEngine.h"
#include "MainWindow.h"
#include "MockTciServer.h"
#include "PskEngine.h"
#include "RttyEngine.h"
#include "TciClient.h"
#include "modemtxsource.h"
#include "rttytxsource.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QSettings>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::printf("  FAIL line %d: %s\n", __LINE__, #cond);                                                      \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
        else                                                                                                           \
            std::printf("  ok   %s\n", #cond);                                                                         \
    } while (0)

static bool waitFor(const std::function<bool()> &cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

static void pump(int ms)
{
    waitFor([] { return false; }, ms);
}

static void runSession(bool stopRxWhileTx)
{
    std::printf("\n=== session: %s ===\n",
                stopRxWhileTx ? "server stops RX audio while transmitting (local TX clock)"
                              : "server keeps RX audio + TX chrono while transmitting");
    MockTciServer server;
    server.stopRxWhileTx = stopRxWhileTx;
    if (!server.listen())
    {
        std::printf("  FAIL: mock server could not listen\n");
        ++g_fail;
        return;
    }

    TciClient client;
    RttyEngine rtty;
    CwDecoder cw;
    PskEngine psk;
    HellEngine hell;
    std::shared_ptr<RttyTxSource> tx = rtty.txSource();
    std::shared_ptr<ModemTxSource> pskTx = psk.txSource();
    client.setTxRender(
        [tx, pskTx](float *o, size_t f, int c, float r)
        {
            if (pskTx->active())
                pskTx->render(o, f, c, r);
            else
                tx->render(o, f, c, r);
        });
    QObject::connect(&client, &TciClient::rxAudio, &psk, &ModemEngine::pushAudio);
    QObject::connect(&client, &TciClient::rxAudio, &hell, &ModemEngine::pushAudio);
    QObject::connect(&client, &TciClient::radioTransmitChanged, &psk, &ModemEngine::setRadioTransmitting);
    QObject::connect(&psk, &ModemEngine::transmitRequested, &client, &TciClient::setTransmit);
    QString pskText;
    QObject::connect(&psk, &ModemEngine::textAppended, [&pskText](const QString &t) { pskText += t; });
    int hellCols = 0;
    float hellPeak = 0.0f;
    QObject::connect(&hell,
                     &HellEngine::columns,
                     [&hellCols, &hellPeak](const QVector<float> &c)
                     {
                         hellCols += int(c.size()) / hell::kRows;
                         for (float v : c)
                             hellPeak = std::max(hellPeak, v);
                     });
    QObject::connect(&client, &TciClient::rxAudio, &rtty, &RttyEngine::pushAudio);
    QObject::connect(&client, &TciClient::rxAudio, &cw, &CwDecoder::pushAudio);
    QObject::connect(&client, &TciClient::radioTransmitChanged, &rtty, &RttyEngine::setRadioTransmitting);
    QObject::connect(&client, &TciClient::modulationChanged, &rtty, &RttyEngine::setRadioMode);
    QObject::connect(&rtty, &RttyEngine::transmitRequested, &client, &TciClient::setTransmit);
    QString diag;
    QObject::connect(&client, &TciClient::diagnostics, [&diag](const QString &d) { diag = d; });
    QString note;
    QObject::connect(&client, &TciClient::logLine, [&note](const QString &d) { note += d; });
    QString rxText;
    QObject::connect(&rtty, &RttyEngine::textAppended, [&rxText](const QString &t) { rxText += t; });
    QString status;
    QObject::connect(&rtty, &RttyEngine::statusChanged, [&status](const QString &s) { status = s; });
    int scopeFrames = 0;
    QObject::connect(&rtty, &RttyEngine::scopePoints, [&scopeFrames](const QVector<float> &) { ++scopeFrames; });

    client.connectToServer(QStringLiteral("127.0.0.1"), server.port());
    CHECK(waitFor([&] { return client.isReady(); }, 3000));
    rtty.setConnected(client.isReady(), !client.receiveOnly());
    CHECK(client.device() == QStringLiteral("IC-7610"));
    CHECK(client.frequencyHz() == 14080000);
    CHECK(client.modulation() == QStringLiteral("DIGU"));
    CHECK(rtty.usbEffective());

    // ---- receive
    rtty.setDecoding(true);
    cw.setEnabled(true);
    CHECK(waitFor([&] { return rxText.contains(QStringLiteral("W6EL W6EL")); }, 12000));
    std::printf("  received: %s\n", qPrintable(rxText.simplified().left(120)));
    CHECK(note.contains(QStringLiteral("announced 2")));
    CHECK(scopeFrames > 10);
    CHECK(diag.contains(QStringLiteral("48000 Hz, 2 ch (header 1), 2048 frames/packet")));
    std::printf("  diag: %s\n", qPrintable(diag));

    // auto-tune finds the pair (mark 2295 for USB)
    rtty.setMarkHz(2250);
    rtty.autoTune();
    CHECK(waitFor([&] { return status.startsWith(QStringLiteral("Tuned")); }, 3000));
    std::printf("  %s\n", qPrintable(status));
    CHECK(qAbs(rtty.markHz() - 2295) <= 5);

    // ---- PSK and Hell receive (stations at 1000 and 500 Hz in the same audio)
    psk.loadSettings(nullptr);
    hell.loadSettings(nullptr);
    psk.setConnected(client.isReady(), !client.receiveOnly());
    hell.setConnected(client.isReady(), !client.receiveOnly());
    psk.setCarrierHz(1003.0); // a little off: AFC pulls in
    hell.setCarrierHz(500.0);
    psk.setDecoding(true);
    hell.setDecoding(true);
    CHECK(waitFor([&] { return pskText.contains(QStringLiteral("psk test de W6EL W6EL pse k")); }, 20000));
    std::printf("  PSK received: %s\n", qPrintable(pskText.simplified().left(120)));
    std::printf("  PSK AFC: %.2f Hz\n", psk.carrierHz());
    CHECK(qAbs(psk.carrierHz() - 1000.0) < 1.0);
    CHECK(hellCols > 50);
    CHECK(hellPeak > 0.9f);
    std::printf("  Hell columns painted: %d\n", hellCols);
    hell.setDecoding(false);

    // ---- PSK transmit
    psk.setMyCall(QStringLiteral("KC9OXP"));
    psk.setCall(QStringLiteral("W6EL"));
    psk.setCarrierHz(1000.0);
    psk.sendMacro(1); // "{CALL} {CALL} de {MYCALL} {MYCALL} {MYCALL} k"
    CHECK(psk.transmitting());
    CHECK(waitFor([&] { return client.radioTransmitting(); }, 1000));
    CHECK(waitFor([&] { return !psk.transmitting(); }, 30000));
    std::printf("  server decoded our PSK: %s\n", server.txPskDecoded.c_str());
    CHECK(QString::fromStdString(server.txPskDecoded).contains(QStringLiteral("W6EL W6EL de KC9OXP KC9OXP KC9OXP k")));
    CHECK(waitFor([&] { return !server.transmitting; }, 1000));
    CHECK(pskText.contains(QStringLiteral("[TX] W6EL W6EL de KC9OXP KC9OXP KC9OXP k")));
    psk.setDecoding(false);
    server.txPskDecoded.clear();

    // ---- transmit
    rtty.setMyCall(QStringLiteral("KC9OXP"));
    rtty.setCall(QStringLiteral("W6EL"));
    rtty.sendMacro(1); // "{CALL} {CALL} DE {MYCALL} {MYCALL} {MYCALL} K"
    CHECK(rtty.transmitting());
    CHECK(waitFor([&] { return client.radioTransmitting(); }, 1000));
    CHECK(waitFor([&] { return !rtty.transmitting(); }, 20000));
    std::printf("  status after TX: %s\n", qPrintable(status));
    std::printf("  server decoded our TX: %s\n",
                QString::fromStdString(server.txDecoded).simplified().toLatin1().constData());
    CHECK(QString::fromStdString(server.txDecoded).contains(QStringLiteral("W6EL W6EL DE KC9OXP KC9OXP KC9OXP K")));
    CHECK(server.txFramesWhileNotTx == 0);
    CHECK(waitFor([&] { return !server.transmitting; }, 1000));
    CHECK(waitFor([&] { return !client.radioTransmitting(); }, 1000));
    std::printf("  TX frames received by server: %llu\n", (unsigned long long)server.txFramesReceived);
    CHECK(server.txFramesReceived > 48000 * 3);

    // ---- abort mid-transmission
    rtty.send(QStringLiteral("RYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRY\n"));
    pump(1500);
    rtty.abortTx();
    CHECK(!rtty.transmitting());
    pump(300);
    CHECK(waitFor([&] { return !server.transmitting; }, 1000));

    // ---- mode and frequency commands
    client.setModulation(QStringLiteral("DIGL"));
    CHECK(waitFor([&] { return client.modulation() == QStringLiteral("DIGL"); }, 1000));
    CHECK(!rtty.usbEffective());
    client.setFrequency(7080000);
    CHECK(waitFor([&] { return client.frequencyHz() == 7080000; }, 1000));

    // ---- disconnect while transmitting releases cleanly
    client.setModulation(QStringLiteral("DIGU"));
    rtty.startTx();
    pump(500);
    client.disconnectFromServer();
    CHECK(waitFor([&] { return client.state() == TciClient::State::Disconnected; }, 2000));
    rtty.setConnected(false, false);
    CHECK(!rtty.transmitting());
}

static void renderScreens(const QString &dir)
{
    std::printf("\n=== window screenshots ===\n");
    MockTciServer server;
    server.listen();
    QTemporaryDir tmp;
    QSettings settings(tmp.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("TCI/Port"), server.port());
    settings.setValue(QStringLiteral("TCI/ConnectOnStart"), true);
    settings.setValue(QStringLiteral("Hell/Carrier"), 500.0); // the mock's Hell station
    settings.setValue(QStringLiteral("Station/MyCall"), QStringLiteral("KC9OXP"));
    MainWindow w(&settings);
    w.show();
    pump(1500);
    auto *tabs = w.findChild<QTabWidget *>();
    // Switch Decode on in every tab so the scope, waterfall and decode panes are live.
    for (QPushButton *b : w.findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("Decode") && !b->isChecked())
            b->click();
    pump(12000); // enough for a few Hell words
    const QList<QSize> sizes = {QSize(1100, 760), QSize(640, 520), QSize(1600, 1000)};
    static const char *const names[] = {"cw", "rtty", "psk", "hell"};
    for (int tab = 0; tab < tabs->count() && tab < 4; ++tab)
    {
        tabs->setCurrentIndex(tab);
        for (const QSize &s : sizes)
        {
            w.resize(s);
            pump(1200);
            const QString f = QDir(dir).filePath(
                QStringLiteral("%1-%2x%3.png").arg(names[tab]).arg(s.width()).arg(s.height()));
            w.grab().save(f);
            std::printf("  %s (actual %dx%d)\n", qPrintable(f), w.width(), w.height());
        }
    }
    std::printf("  minimum size: %dx%d\n", w.minimumSizeHint().width(), w.minimumSizeHint().height());
    // My call is shared: set in settings for the new modes, it reaches every tab.
    int calls = 0;
    for (QLineEdit *e : w.findChildren<QLineEdit *>())
        if (e->text() == QStringLiteral("KC9OXP"))
            ++calls;
    std::printf("  My call fields showing KC9OXP: %d\n", calls);
    CHECK(calls == 3); // RTTY, PSK, Hell
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int serve = args.indexOf(QStringLiteral("--serve"));
    if (serve >= 0)
    {
        MockTciServer server;
        const quint16 port = quint16(args.value(serve + 1, QStringLiteral("50001")).toUInt());
        if (!server.listen(port))
        {
            std::printf("cannot listen on %u\n", port);
            return 1;
        }
        std::printf("mock wfview TCI server on 127.0.0.1:%u (Ctrl+C to stop)\n", port);
        return app.exec();
    }

    runSession(false);
    runSession(true);
    const int screens = args.indexOf(QStringLiteral("--screens"));
    if (screens >= 0)
        renderScreens(args.value(screens + 1, QStringLiteral(".")));
    std::printf(g_fail ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
