// End-to-end test of the TCI client, the RTTY and PSK engines, Feld Hell receive and the CW decoder
// against MockTciServer (which behaves like wfview 3.00's TCI server).
//
//   test_e2e            run the tests (about 40 s, real time)
//   test_e2e --serve N  only run the mock server on port N (to try the app without a radio)
//   test_e2e --new      only the tests of the 0.4.0 / 0.4.1 additions
//   test_e2e --screens DIR  also render the main window at several sizes to PNGs in DIR

#include "AmtorEngine.h"
#include "AudioHub.h"
#include "CwDecoder.h"
#include "CwTxEngine.h"
#include "HellEngine.h"
#include "MainWindow.h"
#include "MockTciServer.h"
#include "NavtexEngine.h"
#include "OliviaEngine.h"
#include "PskEngine.h"
#include "QsoLog.h"
#include "RttyEngine.h"
#include "SstvEngine.h"
#include "TciClient.h"
#include "TxPowerWatch.h"
#include "TxTools.h"
#include "WefaxEngine.h"
#include "WwvEngine.h"
#include "modemtxsource.h"
#include "navtexcore.h"
#include "oliviacore.h"
#include "rttytxsource.h"
#include "sstvcore.h"
#include "wavfile.h"
#include "wefaxcore.h"
#include "wwvcore.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
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
    CwTxEngine cwTx;
    std::shared_ptr<RttyTxSource> tx = rtty.txSource();
    std::shared_ptr<ModemTxSource> pskTx = psk.txSource();
    std::shared_ptr<ModemTxSource> cwTxSrc = cwTx.txSource();
    client.setTxRender(
        [tx, pskTx, cwTxSrc](float *o, size_t f, int c, float r)
        {
            if (pskTx->active())
                pskTx->render(o, f, c, r);
            else if (cwTxSrc->active())
                cwTxSrc->render(o, f, c, r);
            else
                tx->render(o, f, c, r);
        });
    QObject::connect(&client, &TciClient::radioTransmitChanged, &cwTx, &ModemEngine::setRadioTransmitting);
    QObject::connect(&cwTx, &ModemEngine::transmitRequested, &client, &TciClient::setTransmit);
    TxPowerWatch watch;
    QObject::connect(&client, &TciClient::txSensors, &watch, &TxPowerWatch::onSensors);
    QObject::connect(&client, &TciClient::radioTransmitChanged, &watch, &TxPowerWatch::setRadioTransmitting);
    QObject::connect(&psk, &ModemEngine::transmitRequested, &watch, &TxPowerWatch::setOurTransmit);
    QObject::connect(&cwTx, &ModemEngine::transmitRequested, &watch, &TxPowerWatch::setOurTransmit);
    int noPower = 0;
    QObject::connect(&watch, &TxPowerWatch::noPower, [&noPower]() { ++noPower; });
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
    // tx_sensors arrived and showed power, so no "no output" hint
    std::printf("  tx_sensors during PSK TX: %d reports, max %.1f W\n", watch.reports(), watch.maxPower());
    CHECK(server.txSensorsEnabled);
    CHECK(watch.reports() >= TxPowerWatch::kMinReports);
    CHECK(watch.maxPower() > 5.0);
    CHECK(noPower == 0);

    // ---- radio ignores our audio (wrong data mode / DATA MOD input): the hint fires
    server.muteTx = true;
    psk.send(QStringLiteral("no power test no power test\n"));
    CHECK(waitFor([&] { return !psk.transmitting(); }, 30000));
    CHECK(waitFor([&] { return !client.radioTransmitting(); }, 1000));
    std::printf(
        "  muted radio: %d reports, max %.1f W, noPower fired %d\n", watch.reports(), watch.maxPower(), noPower);
    CHECK(noPower == 1);
    server.muteTx = false;
    psk.setDecoding(false);
    server.txPskDecoded.clear();

    // ---- CW transmit (audio CW): the server decodes it
    cwTx.loadSettings(nullptr);
    cwTx.setConnected(client.isReady(), !client.receiveOnly());
    cwTx.setMyCall(QStringLiteral("KC9OXP"));
    cwTx.setCall(QStringLiteral("W6EL"));
    cwTx.setWpm(25);
    cwTx.setCarrierHz(650.0);
    server.txCwDecoded.clear();
    QString cwEcho;
    QObject::connect(&cwTx, &ModemEngine::textAppended, [&cwEcho](const QString &t) { cwEcho += t; });
    cwTx.sendMacro(1); // "{CALL} DE {MYCALL} {MYCALL} K "
    CHECK(cwTx.transmitting());
    CHECK(waitFor([&] { return !cwTx.transmitting(); }, 30000));
    pump(1500); // the decoder holds the last word back briefly
    std::printf("  server decoded our CW: %s\n", server.txCwDecoded.c_str());
    std::printf("  CW echo: %s\n", qPrintable(cwEcho.simplified()));
    CHECK(QString::fromStdString(server.txCwDecoded).contains(QStringLiteral("KC9OXP")));
    CHECK(cwEcho.contains(QStringLiteral("W6EL DE KC9OXP KC9OXP K")));
    CHECK(noPower == 1); // CW had power

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

// A WAV with Olivia 8/500, NAVTEX, AMTOR FEC, an SSTV Robot 36 picture and a
// short weather fax, played through AudioHub into the app's decoders.
static QString makeTestWav(const QString &dir)
{
    const double fs = 48000.0;
    std::vector<float> a(size_t(fs), 0.0f), b(960);
    const auto gap = [&](double sec) { a.insert(a.end(), size_t(sec * fs), 0.0f); };
    {
        olivia::Config c;
        c.centreHz = 1500;
        OliviaModulator m;
        m.setConfig(c);
        m.preroll(0.5);
        for (char ch : std::string("OLIVIA TEST de KC9OXP\n"))
            m.queueChar(ch);
        bool tailed = false;
        for (int g = 0; g < 100000; ++g)
        {
            if (m.idle())
            {
                if (tailed)
                    break;
                m.tail(0);
                tailed = true;
            }
            m.render(b.data(), b.size(), fs);
            for (float v : b)
                a.push_back(0.3f * v);
        }
        gap(10.0); // let the Olivia receiver integrate past the end
    }
    {
        NavtexEncoder n;
        n.start("ZCZC AA01\nNAVTEX TEST MESSAGE 42\nNNNN\n", 4.0);
        while (!n.done())
        {
            n.render(b.data(), b.size(), fs);
            for (float v : b)
                a.push_back(0.3f * v);
        }
        gap(1.0);
    }
    {
        AmtorFecModulator m;
        m.setCentreHz(1500);
        m.preroll(3.0);
        for (char ch : std::string("CQ AMTOR E2E DE KC9OXP 73\n"))
            m.queueChar(ch);
        m.tail(0.0);
        for (int g = 0; g < 100000 && !m.idle(); ++g)
        {
            m.render(b.data(), b.size(), fs);
            for (float v : b)
                a.push_back(0.3f * v);
        }
        gap(1.0);
    }
    {
        const sstv::ModeInfo &m = sstv::info(sstv::Robot36);
        std::vector<uint32_t> img(size_t(m.width) * m.height);
        for (int y = 0; y < m.height; ++y)
            for (int x = 0; x < m.width; ++x)
                img[size_t(y) * m.width + x] = (uint32_t(x * 255 / m.width) << 16) | uint32_t(y * 255 / m.height);
        SstvEncoder e;
        e.start(sstv::Robot36, img);
        while (!e.done())
        {
            e.render(b.data(), b.size(), fs);
            for (float v : b)
                a.push_back(0.3f * v);
        }
        gap(2.0);
    }
    {
        const int W = wefax::widthForIoc(576);
        std::vector<uint8_t> img(size_t(W) * 30);
        for (size_t i = 0; i < img.size(); ++i)
            img[i] = uint8_t((i % size_t(W)) * 255 / size_t(W));
        WefaxEncoder f;
        f.start(img, W, 120, 576, 10);
        while (!f.done())
        {
            f.render(b.data(), b.size(), fs);
            for (float v : b)
                a.push_back(0.3f * v);
        }
        gap(2.0);
    }
    const QString path = QDir(dir).filePath(QStringLiteral("newmodes.wav"));
    WavWriter w;
    w.open(path.toStdString(), int(fs));
    w.write(a.data(), a.size());
    w.close();
    std::printf("  test WAV: %.1f s\n", a.size() / fs);
    return path;
}

static QString paneText(QWidget *tab)
{
    QString all;
    for (QPlainTextEdit *e : tab->findChildren<QPlainTextEdit *>())
        all += e->toPlainText();
    return all;
}

static void runNewModes()
{
    std::printf("\n=== new modes: WAV playback into Olivia, NAVTEX, SSTV, WEFAX; logs ===\n");
    QTemporaryDir tmp;
    QSettings settings(tmp.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Station/MyCall"), QStringLiteral("KC9OXP"));
    settings.setValue(QStringLiteral("Paths/Data"), tmp.filePath(QStringLiteral("data")));
    MainWindow w(&settings);
    w.show();
    pump(300);
    auto *tabs = w.findChild<QTabWidget *>();
    for (QPushButton *b : w.findChildren<QPushButton *>())
        if (b->objectName() == QStringLiteral("decodeButton") && !b->isChecked())
            b->click();
    auto *hub = w.findChild<AudioHub *>();
    CHECK(hub != nullptr);
    const QString wavPath = makeTestWav(tmp.path());
    QString err;
    CHECK(hub->play(wavPath, 8.0, &err));
    CHECK(waitFor([&] { return !hub->playing(); }, 60000));
    pump(4000);

    QWidget *oliviaTab = nullptr, *navtexTab = nullptr, *amtorTab = nullptr;
    int readOnlyDivider = -1, amtorIndex = -1, navtexIndex = -1;
    for (int i = 0; i < tabs->count(); ++i)
    {
        if (tabs->tabText(i) == QStringLiteral("Olivia"))
            oliviaTab = tabs->widget(i);
        if (tabs->tabText(i) == QStringLiteral("NAVTEX"))
        {
            navtexTab = tabs->widget(i);
            navtexIndex = i;
        }
        if (tabs->tabText(i) == QStringLiteral("AMTOR"))
        {
            amtorTab = tabs->widget(i);
            amtorIndex = i;
        }
        if (!tabs->isTabEnabled(i))
            readOnlyDivider = i;
    }
    // Transmit tabs, then the "Read only" divider, then the receive-only tabs.
    CHECK(amtorIndex >= 0 && readOnlyDivider > amtorIndex && navtexIndex > readOnlyDivider);
    CHECK(tabs->tabText(tabs->count() - 1) == QStringLiteral("WWV Time"));
    CHECK(w.findChild<WwvEngine *>() != nullptr);
    const QString ol = paneText(oliviaTab), nav = paneText(navtexTab), am = paneText(amtorTab);
    std::printf("  Olivia pane: [%s]\n  NAVTEX pane: [%s]\n  AMTOR pane: [%s]\n",
                qPrintable(ol.trimmed()),
                qPrintable(nav.trimmed()),
                qPrintable(am.trimmed()));
    CHECK(ol.contains(QStringLiteral("OLIVIA TEST de KC9OXP")));
    CHECK(nav.contains(QStringLiteral("NAVTEX TEST MESSAGE 42")));
    CHECK(am.contains(QStringLiteral("CQ AMTOR E2E DE KC9OXP 73")));
    const QDir data(tmp.filePath(QStringLiteral("data")));
    const QStringList sstvFiles = QDir(data.filePath(QStringLiteral("sstv"))).entryList({QStringLiteral("*.png")});
    const QStringList faxFiles = QDir(data.filePath(QStringLiteral("wefax"))).entryList({QStringLiteral("*.png")});
    std::printf("  SSTV files %s\n  WEFAX files %s\n", qPrintable(sstvFiles.join(' ')), qPrintable(faxFiles.join(' ')));
    CHECK(sstvFiles.size() == 1 && sstvFiles.value(0).contains(QStringLiteral("Robot-36")));
    CHECK(faxFiles.size() == 1);
    if (faxFiles.size() == 1)
    {
        const QImage fax(QDir(data.filePath(QStringLiteral("wefax"))).filePath(faxFiles.at(0)));
        std::printf("  WEFAX picture %dx%d\n", fax.width(), fax.height());
        CHECK(fax.width() == 1810 && fax.height() >= 28 && fax.height() <= 40);
    }

    // session log
    const QDir logDir(data.filePath(QStringLiteral("logs")));
    std::printf("  logs: %s\n", qPrintable(logDir.entryList(QDir::Files).join(QStringLiteral(", "))));
    const auto readLog = [&](const QString &mode)
    {
        const QStringList f = logDir.entryList({QStringLiteral("* ") + mode + QStringLiteral(".txt")});
        QFile lf(logDir.filePath(f.value(0)));
        return f.size() == 1 && lf.open(QIODevice::ReadOnly) ? QString::fromUtf8(lf.readAll()) : QString();
    };
    const QString olLog = readLog(QStringLiteral("Olivia"));
    CHECK(olLog.contains(QStringLiteral("--- ")) && olLog.contains(QStringLiteral("Olivia 8/500 @ 1500 Hz")));
    CHECK(olLog.contains(QStringLiteral("OLIVIA TEST de KC9OXP")));
    CHECK(readLog(QStringLiteral("NAVTEX")).contains(QStringLiteral("NAVTEX TEST MESSAGE 42")));
    CHECK(readLog(QStringLiteral("AMTOR")).contains(QStringLiteral("AMTOR E2E")));
    CHECK(readLog(QStringLiteral("SSTV")).contains(QStringLiteral("[SSTV] received Robot 36")));
    CHECK(readLog(QStringLiteral("WEFAX")).contains(QStringLiteral("[WEFAX]")));

    // QSO log on the Olivia tab
    tabs->setCurrentWidget(oliviaTab);
    auto *qso = w.findChild<QsoLogDock *>();
    qso->setCall(QStringLiteral("W6EL"));
    for (QPushButton *b : qso->findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("Log QSO"))
            b->click();
    QFile af(qso->logPath());
    const QList<Qso> q = af.open(QIODevice::ReadOnly) ? Adif::parse(QString::fromUtf8(af.readAll())) : QList<Qso>();
    CHECK(q.size() == 1);
    if (!q.isEmpty())
    {
        std::printf("  ADIF: %s", qPrintable(Adif::record(q.first())));
        CHECK(q.first().call == QStringLiteral("W6EL"));
        CHECK(q.first().mode == QStringLiteral("OLIVIA") && q.first().submode == QStringLiteral("OLIVIA 8/500"));
        CHECK(q.first().myCall == QStringLiteral("KC9OXP"));
        CHECK(q.first().on.isValid());
    }

    // window screenshot (F12)
    for (QAction *a : w.findChildren<QAction *>())
        if (a->text() == QStringLiteral("Save window screenshot"))
            a->trigger();
    CHECK(QDir(data.filePath(QStringLiteral("screenshots"))).entryList({QStringLiteral("window-*.png")}).size() == 1);
}

static void runNewModesTx()
{
    std::printf("\n=== new modes: Olivia and SSTV transmit to the mock server; save last audio ===\n");
    MockTciServer server;
    server.listen();
    QTemporaryDir tmp;
    QSettings settings(tmp.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("TCI/Port"), server.port());
    settings.setValue(QStringLiteral("TCI/ConnectOnStart"), true);
    settings.setValue(QStringLiteral("Station/MyCall"), QStringLiteral("KC9OXP"));
    settings.setValue(QStringLiteral("Paths/Data"), tmp.filePath(QStringLiteral("data")));
    MainWindow w(&settings);
    w.show();
    auto *client = w.findChild<TciClient *>();
    CHECK(waitFor([&] { return client->isReady(); }, 5000));
    pump(2500);

    auto *hub = w.findChild<AudioHub *>();
    QString err;
    const QString saved = hub->saveLast(60, &err);
    std::printf("  saved last audio: %s %s\n", qPrintable(saved), qPrintable(err));
    std::vector<float> back;
    double rate = 0;
    std::string e;
    CHECK(!saved.isEmpty() && wav::read(saved.toStdString(), back, rate, e) && rate == 48000.0 && back.size() > 48000);

    auto *ol = w.findChild<OliviaEngine *>();
    const quint64 before = server.txFramesReceived;
    ol->send(QStringLiteral("TEST"));
    CHECK(ol->transmitting());
    CHECK(waitFor([&] { return !ol->transmitting(); }, 30000)); // releases by itself when sent
    std::printf("  Olivia TX frames %llu\n", (unsigned long long)(server.txFramesReceived - before));
    CHECK(server.txFramesReceived - before > 48000 * 3);
    CHECK(waitFor([&] { return !client->radioTransmitting(); }, 2000));

    auto *sstvEng = w.findChild<SstvEngine *>();
    sstvEng->setTxMode(sstv::Robot36);
    QImage pic(320, 240, QImage::Format_RGB32);
    pic.fill(Qt::blue);
    const quint64 before2 = server.txFramesReceived;
    sstvEng->sendImage(pic);
    CHECK(sstvEng->transmitting());
    CHECK(waitFor([&] { return server.txFramesReceived - before2 > 48000 * 4; }, 10000));
    std::printf("  SSTV progress %.3f of %.1f s\n", sstvEng->txProgress(), sstvEng->txSeconds());
    CHECK(sstvEng->txProgress() > 0.05 && sstvEng->txSeconds() > 36.0);
    sstvEng->abortTx();
    CHECK(!sstvEng->transmitting());
    CHECK(waitFor([&] { return !client->radioTransmitting(); }, 2000));

    std::printf("\n=== 0.4.1: AMTOR transmit, Tune tone, CW ID ({CWID}) ===\n");
    auto *amtor = w.findChild<AmtorEngine *>();
    CHECK(amtor != nullptr);
    const quint64 before3 = server.txFramesReceived;
    amtor->send(QStringLiteral("CQ CQ DE KC9OXP"));
    CHECK(amtor->transmitting());
    CHECK(waitFor([&] { return !amtor->transmitting(); }, 30000)); // phasing, text, end of emission
    std::printf("  AMTOR TX frames %llu\n", (unsigned long long)(server.txFramesReceived - before3));
    CHECK(server.txFramesReceived - before3 > 48000 * 3);
    CHECK(waitFor([&] { return !client->radioTransmitting(); }, 2000));

    TxTools *tools = TxTools::instance();
    CHECK(tools != nullptr);
    tools->startTune(0.2);
    CHECK(tools->tuning());
    CHECK(waitFor([&] { return client->radioTransmitting(); }, 3000));
    pump(800);
    tools->stopTune();
    CHECK(waitFor([&] { return !tools->tuning(); }, 2000));
    CHECK(waitFor([&] { return !client->radioTransmitting(); }, 2000));

    // {CWID}: the PSK text, then the CW ID without dropping PTT, then release.
    auto *psk = w.findChild<PskEngine *>();
    int pttDrops = 0;
    bool wasTx = false;
    QMetaObject::Connection c = QObject::connect(client,
                                                 &TciClient::radioTransmitChanged,
                                                 [&](bool on)
                                                 {
                                                     if (wasTx && !on)
                                                         ++pttDrops;
                                                     wasTx = on;
                                                 });
    psk->send(QStringLiteral("TEST {CWID}"));
    CHECK(psk->transmitting());
    CHECK(waitFor([&] { return tools->cwIdEngine()->transmitting(); }, 20000));
    CHECK(waitFor([&] { return !tools->cwIdEngine()->transmitting(); }, 20000));
    CHECK(waitFor([&] { return !client->radioTransmitting(); }, 2000));
    QObject::disconnect(c);
    std::printf("  PTT drops during PSK + CW ID: %d\n", pttDrops);
    CHECK(pttDrops == 1);
}

// WWV Time: 160 s of synthesized WWV straight into the engine (its own worker thread).
static void runWwv()
{
    std::printf("\n=== 0.5.0: WWV Time engine decodes and verifies a time ===\n");
    QTemporaryDir tmp;
    QSettings settings(tmp.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    WwvEngine eng(&settings);
    eng.setGrid(QStringLiteral("EN71ar"));
    eng.setDecoding(true);
    wwv::Time t;
    t.year = 2026;
    t.dayOfYear = 319;
    t.hour = 22;
    t.minute = 40;
    t.dut1 = 0.1;
    WwvEncoder enc;
    enc.start(t, 30, false);
    std::vector<float> b(960);
    for (int i = 0; i < 50 * 160; ++i)
    {
        enc.render(b.data(), b.size(), 48000.0);
        QVector<float> v(int(b.size()));
        for (int k = 0; k < v.size(); ++k)
            v[k] = 0.3f * b[size_t(k)];
        eng.pushAudio(v, 48000.0f);
        if (i % 500 == 499)
            pump(5);
    }
    CHECK(waitFor([&] { return eng.snap().st.timeVerified; }, 60000));
    pump(500);
    const WwvSnapshot s = eng.snap();
    std::printf("  ticks %d code %d verified %d: day %d %02d:%02d:%02d, clock %+.1f ppm\n",
                s.st.ticks,
                s.st.code,
                s.st.timeVerified,
                s.st.time.dayOfYear,
                s.st.time.hour,
                s.st.time.minute,
                s.st.secondInMinute,
                s.st.clockPpm);
    CHECK(s.st.ticks && s.st.code);
    CHECK(s.st.time.dayOfYear == 319 && s.st.time.hour == 22 && s.st.time.minute >= 42 && s.st.time.minute <= 43);
    CHECK(std::fabs(s.st.clockPpm) < 3.0);
    double ms = 0, km = 0;
    CHECK(eng.pathDelay(ms, km) && km > 1500 && km < 1750);
    CHECK(WwvEngine::nearestStandard(9999000) == 10000000 && WwvEngine::nearestStandard(14074000) == 0);
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
    settings.setValue(QStringLiteral("Paths/Data"), tmp.filePath(QStringLiteral("data")));
    MainWindow w(&settings);
    w.show();
    pump(1500);
    auto *tabs = w.findChild<QTabWidget *>();
    // Switch Decode on in every tab so the scope, waterfall and decode panes are live.
    for (QPushButton *b : w.findChildren<QPushButton *>())
        if (b->objectName() == QStringLiteral("decodeButton") && !b->isChecked())
            b->click();
    pump(12000); // enough for a few Hell words
    const QList<QSize> sizes = {QSize(1100, 760), QSize(640, 520), QSize(1600, 1000)};
    static const char *const names[]
        = {"cw", "rtty", "psk", "hell", "olivia", "sstv", "amtor", "divider", "navtex", "wefax"};
    for (int tab = 0; tab < tabs->count() && tab < 10; ++tab)
    {
        if (!tabs->isTabEnabled(tab))
            continue;
        tabs->setCurrentIndex(tab);
        std::printf("  tab %s minimum %dx%d\n",
                    names[tab],
                    tabs->widget(tab)->minimumSizeHint().width(),
                    tabs->widget(tab)->minimumSizeHint().height());
        for (const QSize &s : sizes)
        {
            w.resize(s);
            pump(1200);
            const QString f
                = QDir(dir).filePath(QStringLiteral("%1-%2x%3.png").arg(names[tab]).arg(s.width()).arg(s.height()));
            w.grab().save(f);
            std::printf("  %s (actual %dx%d)\n", qPrintable(f), w.width(), w.height());
        }
    }
    std::printf("  minimum size: %dx%d\n", w.minimumSizeHint().width(), w.minimumSizeHint().height());
    if (QWidget *hint = w.findChild<QWidget *>(QStringLiteral("txHint")))
    {
        tabs->setCurrentIndex(2);
        w.resize(QSize(1100, 760));
        hint->show();
        pump(600);
        const QString f = QDir(dir).filePath(QStringLiteral("no-power-hint-1100x760.png"));
        w.grab().save(f);
        std::printf("  %s\n", qPrintable(f));
        hint->hide();
    }
    // My call is shared: set in settings for the new modes, it reaches every tab.
    int calls = 0;
    for (QLineEdit *e : w.findChildren<QLineEdit *>())
        if (e->text() == QStringLiteral("KC9OXP"))
            ++calls;
    std::printf("  My call fields showing KC9OXP: %d\n", calls);
    CHECK(calls == 6); // RTTY, PSK, Hell, CW, Olivia, AMTOR
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

    const bool onlyNew = args.contains(QStringLiteral("--new"));
    if (!onlyNew)
    {
        runSession(false);
        runSession(true);
    }
    runNewModes();
    runNewModesTx();
    runWwv();
    const int screens = args.indexOf(QStringLiteral("--screens"));
    if (screens >= 0)
        renderScreens(args.value(screens + 1, QStringLiteral(".")));
    std::printf(g_fail ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
