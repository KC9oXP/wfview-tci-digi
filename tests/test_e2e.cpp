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
#include "FreqEdit.h"
#include "HellEngine.h"
#include "LogUploader.h"
#include "MainWindow.h"
#include "MemoriesDialog.h"
#include "MockTciServer.h"
#include "NavtexEngine.h"
#include "OliviaEngine.h"
#include "PskEngine.h"
#include "PskReporter.h"
#include "GridTrackerLink.h"
#include "QsoLog.h"
#include "RttyEngine.h"
#include "RxText.h"
#include "SecretStore.h"
#include "SstvEngine.h"
#include "TciClient.h"
#include "TxPowerWatch.h"
#include "TxTools.h"
#include "WefaxEngine.h"
#include "WwvEngine.h"
#include "modemtxsource.h"
#include "navtexcore.h"
#include "mt63core.h"
#include "oliviacore.h"
#include "rsidcore.h"
#include "rttytxsource.h"
#include "sstvcore.h"
#include "wavfile.h"
#include "wefaxcore.h"
#include "wwvcore.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QUdpSocket>
#include <QUrlQuery>
#include <QWheelEvent>
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
    {
        // An RSID burst for MT63-1000L at 1500 Hz, then MT63 traffic (0.9.14):
        // the app switches to the MT63 tab and decodes it.
        const std::vector<float> id = rsid::makeBurst(12, 1500.0, fs);
        for (float v : id)
            a.push_back(0.3f * v);
        mt63::Config c;
        Mt63Modulator m;
        m.setConfig(c);
        m.preroll(0.0);
        for (char ch : std::string("MT63 TEST de KC9OXP\n"))
            m.queueChar(ch);
        m.tail(0.0);
        for (int g = 0; g < 100000 && !m.idle(); ++g)
        {
            m.render(b.data(), b.size(), fs);
            for (float v : b)
                a.push_back(0.3f * v);
        }
        gap(mt63::delaySeconds(c) + 4.0); // the last characters come out one interleave later
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

    QWidget *oliviaTab = nullptr, *navtexTab = nullptr, *amtorTab = nullptr, *mt63Tab = nullptr;
    int readOnlyDivider = -1, amtorIndex = -1, navtexIndex = -1;
    for (int i = 0; i < tabs->count(); ++i)
    {
        if (tabs->tabText(i) == QStringLiteral("Olivia"))
            oliviaTab = tabs->widget(i);
        if (tabs->tabText(i) == QStringLiteral("MT63"))
            mt63Tab = tabs->widget(i);
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
    // MT63 after an RSID burst: the tab was switched to, the ID noted, the text decoded.
    CHECK(mt63Tab != nullptr);
    const QString mt = mt63Tab ? paneText(mt63Tab) : QString();
    std::printf("  MT63 pane: [%s]\n", qPrintable(mt.trimmed()));
    CHECK(mt.contains(QStringLiteral("RSID: MT63-1000L at 15")));
    CHECK(mt.contains(QStringLiteral("MT63 TEST de KC9OXP")));
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
    CHECK(readLog(QStringLiteral("MT63")).contains(QStringLiteral("MT63 TEST de KC9OXP")));
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

    // "Fill RST sent with a measured report": only on the tabs that can measure one (SSTV, PSK).
    {
        QCheckBox *measured = nullptr;
        for (QCheckBox *c : qso->findChildren<QCheckBox *>())
            if (c->text().startsWith(QStringLiteral("Fill RST sent with a measured report")))
                measured = c;
        CHECK(measured != nullptr);
        if (measured)
        {
            CHECK(measured->isChecked()); // on by default
            const auto hiddenOn = [&](const QString &tab)
            {
                for (int i = 0; i < tabs->count(); ++i)
                    if (tabs->tabText(i) == tab)
                        tabs->setCurrentIndex(i);
                pump(50);
                return measured->isHidden();
            };
            CHECK(hiddenOn(QStringLiteral("Olivia")));
            CHECK(!hiddenOn(QStringLiteral("SSTV")));
            CHECK(!hiddenOn(QStringLiteral("PSK")));
            CHECK(hiddenOn(QStringLiteral("CW")));
            CHECK(hiddenOn(QStringLiteral("RTTY")));
            CHECK(hiddenOn(QStringLiteral("MT63")));
            tabs->setCurrentWidget(oliviaTab);
        }
    }

    // CW multi-signal list: picking a row copies what it decoded so far into the main pane, once.
    {
        QWidget *cwTab = nullptr;
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == QStringLiteral("CW"))
                cwTab = tabs->widget(i);
        auto *cwDec = w.findChild<CwDecoder *>();
        CHECK(cwTab != nullptr && cwDec != nullptr);
        if (cwTab && cwDec)
        {
            cwDec->decodeChannel(733, QStringLiteral("CQ CQ DE W1AW W1AW K"));
            cwDec->decodeChannel(733, QStringLiteral("CQ CQ DE W1AW W1AW K")); // 2nd click of a double-click
            pump(100);
            const QString cwText = paneText(cwTab);
            std::printf("  CW pane after picking 733 Hz: [%s]\n", qPrintable(cwText.trimmed().right(120)));
            CHECK(cwText.contains(QStringLiteral("[now decoding 733 Hz; copied so far:]")));
            CHECK(cwText.count(QStringLiteral("CQ CQ DE W1AW W1AW K")) == 1);
            CHECK(cwDec->fixedPitch() && cwDec->pitchHz() == 733);
        }
    }

    // Options > Language: System language (the default, checked) and English
    {
        int found = 0;
        for (QAction *a : w.findChildren<QAction *>())
        {
            if (a->text() == QStringLiteral("System language") && a->isCheckable() && a->isChecked())
                ++found;
            if (a->text() == QStringLiteral("English") && a->isCheckable() && !a->isChecked())
                ++found;
        }
        CHECK(found == 2);
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

    std::printf("\n=== 0.9.3: Auto TX level sets the RTTY TX level for ALC 0.1 ===\n");
    // Mock radio: ALC from -12 dBFS peak, 0.08 per dB, one decimal -> highest level
    // reading 0.1 is -10.5 dB.
    server.alcFromLevel = true;
    auto *rtty = w.findChild<RttyEngine *>();
    CHECK(rtty != nullptr);
    rtty->setTxLevel(std::pow(10.0, -30.0 / 20.0));
    tools->setAutoLevelTarget(0.1);
    TxTools::TestSignal sig;
    sig.hz = rtty->markHz();
    sig.hz2 = rtty->spaceHz();
    sig.holdSec = 1.0 / rtty->baud();
    tools->startAutoLevel(rtty->txLevel(), sig, [rtty](double level) { rtty->setTxLevel(level); });
    CHECK(tools->autoLeveling());
    CHECK(waitFor([&] { return !tools->autoLeveling(); }, 60000));
    const double db = 20.0 * std::log10(rtty->txLevel());
    std::printf("  RTTY TX level after Auto TX level: %.1f dB\n", db);
    CHECK(db >= -11.5 && db <= -10.0);
    CHECK(waitFor([&] { return !client->radioTransmitting(); }, 3000));

    std::printf("  target 0: first finds ALC, then backs off to none\n");
    rtty->setTxLevel(std::pow(10.0, -30.0 / 20.0));
    tools->setAutoLevelTarget(0.0);
    tools->startAutoLevel(rtty->txLevel(), sig, [rtty](double level) { rtty->setTxLevel(level); });
    CHECK(waitFor([&] { return !tools->autoLeveling(); }, 60000));
    const double db0 = 20.0 * std::log10(rtty->txLevel());
    std::printf("  RTTY TX level for ALC 0: %.1f dB\n", db0);
    CHECK(db0 >= -13.0 && db0 <= -11.0); // ALC 0 below about -11.4 dB
    CHECK(waitFor([&] { return !client->radioTransmitting(); }, 3000));
    std::printf("  from full level (0 dB), server reporting ALC only when it changes\n");
    server.alcOnChangeOnly = true;
    rtty->setTxLevel(1.0);
    tools->setAutoLevelTarget(0.1);
    tools->startAutoLevel(rtty->txLevel(), sig, [rtty](double level) { rtty->setTxLevel(level); });
    CHECK(waitFor([&] { return !tools->autoLeveling(); }, 70000));
    const double dbc = 20.0 * std::log10(rtty->txLevel());
    std::printf("  RTTY TX level: %.1f dB\n", dbc);
    CHECK(dbc >= -11.5 && dbc <= -10.0);
    CHECK(waitFor([&] { return !client->radioTransmitting(); }, 3000));
    server.alcOnChangeOnly = false;
    server.alcFromLevel = false;
}

// WWV Time: 200 s of synthesized WWV straight into the engine (its own worker thread).
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
    for (int i = 0; i < 50 * 200; ++i)
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
        = {"cw", "rtty", "psk", "hell", "olivia", "mt63", "sstv", "amtor", "divider", "navtex", "wefax"};
    for (int tab = 0; tab < tabs->count() && tab < 11; ++tab)
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
    CHECK(calls == 7); // RTTY, PSK, Hell, CW, Olivia, MT63, AMTOR
}

static void runMemories()
{
    std::printf("\n=== 0.6.0: memories window tunes tab, sub-mode, dial and sideband ===\n");
    MockTciServer server;
    server.listen();
    QTemporaryDir tmp;
    QSettings settings(tmp.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("TCI/Port"), server.port());
    settings.setValue(QStringLiteral("TCI/ConnectOnStart"), true);
    settings.setValue(QStringLiteral("Paths/Data"), tmp.filePath(QStringLiteral("data")));
    MainWindow w(&settings);
    w.show();
    auto *client = w.findChild<TciClient *>();
    CHECK(waitFor([&] { return client->isReady(); }, 5000));
    pump(500);
    auto *tabs = w.findChild<QTabWidget *>();
    tabs->setCurrentIndex(0); // CW

    QPushButton *memBtn = nullptr;
    for (QPushButton *b : w.findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("Memories"))
            memBtn = b;
    CHECK(memBtn != nullptr);
    if (!memBtn)
        return;
    memBtn->click();
    pump(300);
    auto *dlg = w.findChild<MemoriesDialog *>();
    CHECK(dlg != nullptr && dlg->isVisible());
    if (!dlg)
        return;
    // First open: memories.csv made from the built-in list.
    const QString path = MemoriesDialog::filePath();
    std::printf("  file %s\n", qPrintable(QDir::toNativeSeparators(path)));
    CHECK(path.startsWith(tmp.filePath(QStringLiteral("data"))));
    CHECK(QFileInfo::exists(path));
    CHECK(MemoriesDialog::builtIn().size() >= 40);
    auto *table = dlg->findChild<QTableWidget *>();
    CHECK(table && table->rowCount() == int(MemoriesDialog::builtIn().size()));

    const auto rowOf = [&](const QString &name)
    {
        for (int r = 0; r < table->rowCount(); ++r)
            if (table->item(r, 0)->text() == name)
                return r;
        return -1;
    };
    const auto click = [&](const QString &name)
    {
        const int r = rowOf(name);
        if (r < 0)
            return false;
        table->selectRow(r);
        emit table->cellClicked(r, 0);
        return true;
    };

    // PSK31 20 m: PSK tab, BPSK31, 14.070 USB
    auto *psk = w.findChild<PskEngine *>();
    psk->setSpeed(psk::Bpsk63);
    CHECK(click(QStringLiteral("PSK31 20 m")));
    CHECK(waitFor([&] { return server.freq == 14070000; }, 3000));
    CHECK(tabs->tabText(tabs->currentIndex()) == QStringLiteral("PSK"));
    CHECK(psk->speed() == psk::Bpsk31);

    // RTTY 40 m: RTTY tab, 45.45 / 170
    auto *rtty = w.findChild<RttyEngine *>();
    rtty->setBaud(75);
    CHECK(click(QStringLiteral("RTTY 40 m")));
    CHECK(waitFor([&] { return server.freq == 7080000; }, 3000));
    CHECK(tabs->tabText(tabs->currentIndex()) == QStringLiteral("RTTY"));
    CHECK(std::fabs(rtty->baud() - 45.45) < 0.01 && rtty->shift() == 170);

    // SSTV 40 m: LSB (stays a data mode: digl)
    CHECK(click(QStringLiteral("SSTV 40 m")));
    CHECK(waitFor([&] { return server.freq == 7171000; }, 3000));
    CHECK(waitFor([&] { return server.lastMode.toLower() == QStringLiteral("digl"); }, 3000));
    std::printf("  mode after SSTV 40 m: %s\n", qPrintable(server.lastMode));

    // Classes: the 40 m SSTV spot is Advanced/Extra only, so a General filter hides it.
    QComboBox *classBox = nullptr;
    for (QComboBox *c : dlg->findChildren<QComboBox *>())
        if (c->findText(QStringLiteral("Any class")) >= 0)
            classBox = c;
    CHECK(classBox != nullptr);
    if (classBox)
    {
        CHECK(rowOf(QStringLiteral("SSTV 40 m")) >= 0);
        classBox->setCurrentIndex(classBox->findText(QStringLiteral("General")));
        CHECK(rowOf(QStringLiteral("SSTV 40 m")) < 0);
        CHECK(rowOf(QStringLiteral("SSTV 20 m")) >= 0);
        CHECK(rowOf(QStringLiteral("NAVTEX 518 kHz")) >= 0); // receive-only always listed
        classBox->setCurrentIndex(classBox->findText(QStringLiteral("Technician")));
        CHECK(rowOf(QStringLiteral("PSK31 10 m")) >= 0);
        CHECK(rowOf(QStringLiteral("PSK31 20 m")) < 0);
        classBox->setCurrentIndex(0);
    }

    // Edited outside the app (Excel): a new row shows up.
    {
        QFile f(path);
        CHECK(f.open(QIODevice::Append));
        f.write("My net,Olivia,Olivia 16/500,7.0725,USB,,,Sunday net\r\n");
    }
    CHECK(waitFor([&] { return rowOf(QStringLiteral("My net")) >= 0; }, 5000));
    auto *olivia = w.findChild<OliviaEngine *>();
    CHECK(click(QStringLiteral("My net")));
    CHECK(waitFor([&] { return server.freq == 7072500; }, 3000));
    CHECK(olivia->tones() == 16 && olivia->bandwidth() == 500 && !olivia->contestia());
    const int netRow = rowOf(QStringLiteral("My net"));
    CHECK(netRow >= 0 && table->item(netRow, 6)->text() == QStringLiteral("General, Advanced, Extra"));

    // 0.7.0: the Other class (outside the US amateur rules)
    {
        QFile f(path);
        CHECK(f.open(QIODevice::Append));
        f.write("CB 19,SSTV,,27.185,AM,,Other,11 m\r\n");
    }
    CHECK(waitFor([&] { return rowOf(QStringLiteral("CB 19")) >= 0; }, 5000));
    if (classBox)
    {
        CHECK(classBox->findText(QStringLiteral("Other")) >= 0);
        classBox->setCurrentIndex(classBox->findText(QStringLiteral("Other")));
        CHECK(rowOf(QStringLiteral("CB 19")) >= 0);
        CHECK(rowOf(QStringLiteral("PSK31 20 m")) < 0);
        classBox->setCurrentIndex(classBox->findText(QStringLiteral("General")));
        CHECK(rowOf(QStringLiteral("CB 19")) < 0);
        classBox->setCurrentIndex(0);
    }
    bool hasNew = false;
    for (QPushButton *b : dlg->findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("New..."))
            hasNew = true;
    CHECK(hasNew);
    dlg->close();
}

static void runPskReporterAndFreqBox()
{
    std::printf("\n=== 0.7.0: frequency box, click-to-log, PSK Reporter datagram ===\n");
    MockTciServer server;
    server.listen();
    QUdpSocket fake; // stands in for report.pskreporter.info
    CHECK(fake.bind(QHostAddress::LocalHost, 0));
    QTemporaryDir tmp;
    QSettings settings(tmp.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("TCI/Port"), server.port());
    settings.setValue(QStringLiteral("TCI/ConnectOnStart"), true);
    settings.setValue(QStringLiteral("Station/MyCall"), QStringLiteral("KC9OXP"));
    settings.setValue(QStringLiteral("Station/MyGrid"), QStringLiteral("EN71ar"));
    settings.setValue(QStringLiteral("Paths/Data"), tmp.filePath(QStringLiteral("data")));
    settings.setValue(QStringLiteral("PskReporter/DebugHost"), QStringLiteral("127.0.0.1"));
    settings.setValue(QStringLiteral("PskReporter/DebugPort"), fake.localPort());
    MainWindow w(&settings);
    w.show();
    auto *client = w.findChild<TciClient *>();
    CHECK(waitFor([&] { return client->isReady(); }, 5000));
    pump(500);

    // Frequency box: 9 digits with leading zeros; a click steps a digit and never starts typing.
    auto *freq = w.findChild<FreqEdit *>();
    CHECK(freq != nullptr);
    if (freq)
    {
        CHECK(waitFor([&] { return freq->text() == QStringLiteral("014.080.000"); }, 3000));
        std::printf("  box shows %s\n", qPrintable(freq->text()));
        const qint64 before = server.freq;
        const QPointF mid(freq->width() / 2.0, 3); // top half of a middle digit: step up
        QMouseEvent press(
            QEvent::MouseButtonPress, mid, freq->mapToGlobal(mid), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(freq, &press);
        QMouseEvent release(
            QEvent::MouseButtonRelease, mid, freq->mapToGlobal(mid), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(freq, &release);
        CHECK(!freq->hasFocus()); // no text cursor
        CHECK(waitFor([&] { return server.freq > before; }, 2000));
        std::printf("  click: %lld -> %lld Hz\n", (long long)before, (long long)server.freq);
        const qint64 after = server.freq;
        QWheelEvent wheel(mid,
                          freq->mapToGlobal(mid),
                          QPoint(),
                          QPoint(0, -120),
                          Qt::NoButton,
                          Qt::NoModifier,
                          Qt::NoScrollPhase,
                          false);
        QApplication::sendEvent(freq, &wheel);
        CHECK(waitFor([&] { return server.freq < after; }, 2000));
        CHECK(!freq->hasFocus());
        // 160 m: the 10 MHz digit is a 0 that can be turned up
        client->setFrequency(1810000);
        CHECK(waitFor([&] { return freq->text() == QStringLiteral("001.810.000"); }, 3000));
    }

    // Click-to-log: the callsign under a point in a receive pane, and picking it fills the QSO log.
    {
        QPlainTextEdit pane;
        pane.setPlainText(QStringLiteral("CQ CQ DE W1AW/P W1AW/P K"));
        pane.resize(500, 100);
        pane.show();
        pump(100);
        QTextCursor c(pane.document());
        c.setPosition(QStringLiteral("CQ CQ DE W1").size());
        const QPoint at = pane.cursorRect(c).center();
        CHECK(RxCalls::callAt(&pane, at) == QStringLiteral("W1AW/P"));
        c.setPosition(1);
        CHECK(RxCalls::callAt(&pane, pane.cursorRect(c).center()).isEmpty()); // "CQ"
        emit RxCalls::instance() -> callPicked(QStringLiteral("W1AW/P"), QStringLiteral("PSK"));
        auto *qso = w.findChild<QsoLogDock *>();
        bool shown = false;
        for (QLineEdit *e : qso->findChildren<QLineEdit *>())
            if (e->text() == QStringLiteral("W1AW/P"))
                shown = true;
        CHECK(shown && qso->isVisible());
    }

    // PSK Reporter: off at first; turned on with auto-detect; a call seen after CQ / DE is sent.
    auto *pskr = w.findChild<PskReporter *>();
    CHECK(pskr != nullptr);
    if (!pskr)
        return;
    QPushButton *pskrBtn = nullptr;
    for (QPushButton *b : w.findChildren<QPushButton *>())
        if (b->text().startsWith(QStringLiteral("PSKR")))
            pskrBtn = b;
    CHECK(pskrBtn && pskrBtn->text() == QStringLiteral("PSKR off"));
    CHECK(!pskr->enabled());
    CHECK(pskr->configured());
    pskr->setAutoDetect(true);
    pskrBtn->click(); // configured: turns straight on
    CHECK(pskr->enabled());
    CHECK(pskrBtn->text() == QStringLiteral("PSKR 0"));
    client->setFrequency(14070000);
    CHECK(waitFor([&] { return client->frequencyHz() == 14070000; }, 3000));
    pskr->feedText(QStringLiteral("PSK"),
                   PskReporter::modeFor(QStringLiteral("PSK"), QStringLiteral("BPSK31")),
                   QStringLiteral("CQ CQ DE W1AW W1AW W1AW PSE K "),
                   14071000);
    CHECK(pskr->waiting() == 1);
    pskr->sendNow();
    CHECK(waitFor([&] { return fake.hasPendingDatagrams(); }, 3000));
    QByteArray d(int(fake.pendingDatagramSize()), 0);
    fake.readDatagram(d.data(), d.size());
    std::printf("  datagram %d bytes\n", int(d.size()));
    CHECK(d.size() > 16 && quint8(d[0]) == 0x00 && quint8(d[1]) == 0x0A);
    CHECK(((quint8(d[2]) << 8) | quint8(d[3])) == d.size());
    CHECK(d.contains("KC9OXP") && d.contains("EN71ar") && d.contains("W1AW") && d.contains("PSK31"));
    CHECK(d.contains("wfview TCI Digi"));
    CHECK(pskr->spotsSent() == 1 && pskr->waiting() == 0);
    CHECK(waitFor([&] { return pskrBtn->text() == QStringLiteral("PSKR 1"); }, 1000));
    // the same call again straight away: not reported twice
    pskr->feedText(QStringLiteral("PSK"), QStringLiteral("PSK31"), QStringLiteral("CQ DE W1AW W1AW W1AW K "), 14071000);
    CHECK(pskr->waiting() == 0);
    // my own call never; logged QSOs are reported
    pskr->addSpot(QStringLiteral("KC9OXP"), 14070000, QStringLiteral("PSK31"));
    CHECK(pskr->waiting() == 0);
    auto *qso = w.findChild<QsoLogDock *>();
    qso->setCall(QStringLiteral("K3WW"));
    for (QPushButton *b : qso->findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("Log QSO"))
            b->click();
    CHECK(pskr->waiting() == 1);
    // a call picked from a receive pane is reported too
    emit RxCalls::instance() -> callPicked(QStringLiteral("K1ABC"), QStringLiteral("CW"));
    CHECK(pskr->waiting() == 2);
    pskrBtn->click(); // off
    CHECK(!pskr->enabled() && pskrBtn->text() == QStringLiteral("PSKR off"));
}

// GridTracker link: status (dial, mode, call, grid) and heard calls as WSJT-X-format
// datagrams to a local UDP port.
static QList<QByteArray> readAll(QUdpSocket &s)
{
    QList<QByteArray> out;
    while (s.hasPendingDatagrams())
    {
        QByteArray d(int(s.pendingDatagramSize()), 0);
        s.readDatagram(d.data(), d.size());
        out << d;
    }
    return out;
}

static quint32 typeOf(const QByteArray &d)
{
    if (d.size() < 12 || quint8(d[0]) != 0xad || quint8(d[1]) != 0xbc)
        return 0xffffffffU;
    return (quint32(quint8(d[8])) << 24) | (quint32(quint8(d[9])) << 16) | (quint32(quint8(d[10])) << 8)
           | quint32(quint8(d[11]));
}

static void runGridTracker()
{
    std::printf("\n=== 0.9.7: GridTracker live link ===\n");
    MockTciServer server;
    server.listen();
    QUdpSocket gt; // stands in for GridTracker2 on 2237
    CHECK(gt.bind(QHostAddress::LocalHost, 0));
    QTemporaryDir tmp;
    QSettings settings(tmp.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("TCI/Port"), server.port());
    settings.setValue(QStringLiteral("TCI/ConnectOnStart"), true);
    settings.setValue(QStringLiteral("Station/MyCall"), QStringLiteral("KC9OXP"));
    settings.setValue(QStringLiteral("Station/MyGrid"), QStringLiteral("EN71ar"));
    settings.setValue(QStringLiteral("Paths/Data"), tmp.filePath(QStringLiteral("data")));
    settings.setValue(QStringLiteral("GridTracker/Port"), gt.localPort());
    MainWindow w(&settings);
    w.show();
    auto *client = w.findChild<TciClient *>();
    CHECK(waitFor([&] { return client->isReady(); }, 5000));
    auto *link = w.findChild<GridTrackerLink *>();
    CHECK(link != nullptr);
    if (!link)
        return;
    CHECK(!link->enabled()); // off until turned on
    pump(300);
    CHECK(readAll(gt).isEmpty());

    link->setEnabled(true);
    client->setFrequency(7080000);
    CHECK(waitFor([&] { return client->frequencyHz() == 7080000; }, 3000));
    // heartbeat + status arrive; status carries the dial, call and grid
    bool beat = false, status = false;
    CHECK(waitFor(
        [&]
        {
            for (const QByteArray &d : readAll(gt))
            {
                if (typeOf(d) == 0)
                    beat = true;
                // dial 7080000 = 0x00000000006C0840 as quint64
                if (typeOf(d) == 1 && d.contains(QByteArray::fromHex("00000000006c0840")) && d.contains("KC9OXP")
                    && d.contains("EN71ar"))
                    status = true;
            }
            return beat && status;
        },
        3000));
    std::printf("  heartbeat %d, status with dial/call/grid %d\n", int(beat), int(status));

    // a heard call goes out as a Decode, once a minute at most
    link->feedText(QStringLiteral("RTTY"),
                   QStringLiteral("RTTY"),
                   QStringLiteral("CQ CQ DE W1AW W1AW K "),
                   1500,
                   7081500,
                   12.0);
    QByteArray dec;
    CHECK(waitFor(
        [&]
        {
            for (const QByteArray &d : readAll(gt))
                if (typeOf(d) == 2)
                    dec = d;
            return !dec.isEmpty();
        },
        3000));
    CHECK(dec.contains("CQ W1AW") && dec.contains("RTTY"));
    CHECK(link->decodesSent() == 1);
    link->feedText(QStringLiteral("RTTY"), QStringLiteral("RTTY"), QStringLiteral("CQ DE W1AW K "), 1500, 7081500, 12.0);
    CHECK(link->decodesSent() == 1); // not again within the minute
    link->feedText(QStringLiteral("CW"), QStringLiteral("CW"), QStringLiteral("KC9OXP DE K3WW K "), 700, 7030700, 9.0);
    CHECK(link->sentMessages().contains(QStringLiteral("KC9OXP K3WW")));
    link->feedText(QStringLiteral("CW"), QStringLiteral("CW"), QStringLiteral("CQ DE KC9OXP K "), 700, 7030700, 9.0);
    CHECK(link->decodesSent() == 2); // own call never

    link->setEnabled(false);
    pump(300);
    readAll(gt);
    link->feedText(QStringLiteral("CW"), QStringLiteral("CW"), QStringLiteral("CQ DE N0AX K "), 700, 7030700, 9.0);
    pump(300);
    CHECK(readAll(gt).isEmpty());
}

// A stand-in for logbook.qrz.com: plain HTTP on localhost, answers from a list.
class FakeQrz : public QObject
{
public:
    QTcpServer server;
    QStringList bodies;
    QStringList answers; // next answers; "RESULT=OK&LOGID=1&COUNT=1" when empty
    FakeQrz()
    {
        server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server,
                         &QTcpServer::newConnection,
                         this,
                         [this]()
                         {
                             QTcpSocket *s = server.nextPendingConnection();
                             auto *buf = new QByteArray();
                             QObject::connect(
                                 s,
                                 &QTcpSocket::readyRead,
                                 s,
                                 [this, s, buf]()
                                 {
                                     buf->append(s->readAll());
                                     const int he = buf->indexOf("\r\n\r\n");
                                     if (he < 0)
                                         return;
                                     int len = 0;
                                     for (const QByteArray &l : buf->left(he).split('\n'))
                                         if (l.toLower().startsWith("content-length:"))
                                             len = l.mid(15).trimmed().toInt();
                                     if (buf->size() < he + 4 + len)
                                         return;
                                     bodies << QString::fromUtf8(buf->mid(he + 4, len));
                                     const QByteArray a
                                         = (answers.isEmpty() ? QStringLiteral("RESULT=OK&LOGID=1&COUNT=1")
                                                              : answers.takeFirst())
                                               .toUtf8();
                                     s->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
                                              + QByteArray::number(a.size()) + "\r\nConnection: close\r\n\r\n" + a);
                                     s->disconnectFromHost();
                                     buf->clear();
                                 });
                             QObject::connect(s,
                                              &QTcpSocket::disconnected,
                                              s,
                                              [s, buf]()
                                              {
                                                  delete buf;
                                                  s->deleteLater();
                                              });
                         });
    }
};

static QString fakeTqslPath()
{
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString &n : {QStringLiteral("fake_tqsl.exe"), QStringLiteral("fake_tqsl")})
        for (const QString &d : {app.path(), app.filePath(QStringLiteral(".."))})
            if (QFileInfo(QDir(d).filePath(n)).isFile())
                return QDir(d).absoluteFilePath(n);
    return QString();
}

static void runUploads()
{
    std::printf("\n=== 0.8.0: logged QSO to GridTracker (UDP ADIF), QRZ and LoTW; secrets never shown ===\n");
    SecretStore::setTestMode(true); // never touch the real Credential Manager

    // protected memory: round trip, last 4
    {
        SecretBuffer b("hello", 5);
        QString seen;
        b.reveal([&](const char *p, size_t n) { seen = QString::fromUtf8(p, int(n)); });
        CHECK(seen == QStringLiteral("hello") && b.tail4() == QStringLiteral("ello") && b.size() == 5);
    }

    MockTciServer server;
    server.listen();
    QUdpSocket gridTracker;
    CHECK(gridTracker.bind(QHostAddress::LocalHost, 0));
    FakeQrz qrz;
    LogUploader::setQrzUrlForTests(QUrl(QStringLiteral("http://127.0.0.1:%1/api").arg(qrz.server.serverPort())));
    const QString fake = fakeTqslPath();
    std::printf("  fake TQSL: %s\n", fake.isEmpty() ? "(not built: LoTW part skipped)" : qPrintable(fake));
    LogUploader::setTqslPathForTests(fake);
    QTemporaryDir tmp;
    const QString tqslLog = tmp.filePath(QStringLiteral("tqsl.log"));
    qputenv("FAKE_TQSL_LOG", QFile::encodeName(tqslLog));
    qputenv("FAKE_TQSL_STATUS", "0");

    QSettings settings(tmp.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("TCI/Port"), server.port());
    settings.setValue(QStringLiteral("TCI/ConnectOnStart"), true);
    settings.setValue(QStringLiteral("Station/MyCall"), QStringLiteral("KC9OXP"));
    settings.setValue(QStringLiteral("Station/MyGrid"), QStringLiteral("EN71ar"));
    settings.setValue(QStringLiteral("Paths/Data"), tmp.filePath(QStringLiteral("data")));
    settings.setValue(QStringLiteral("Upload/UdpEnabled"), true);
    settings.setValue(QStringLiteral("Upload/UdpHost"), QStringLiteral("127.0.0.1"));
    settings.setValue(QStringLiteral("Upload/UdpPort"), gridTracker.localPort());
    settings.setValue(QStringLiteral("Upload/QrzEnabled"), true);
    settings.setValue(QStringLiteral("Upload/LotwEnabled"), !fake.isEmpty());
    settings.setValue(QStringLiteral("Upload/LotwLocation"), QStringLiteral("Home EN71ar"));
    const QString keyText = QStringLiteral("ABCD-0A0B-1C1D-2E2F");
    const QString pwText = QStringLiteral("pa ss&word -x");
    {
        QString k = keyText, p = pwText;
        CHECK(SecretStore::save(QStringLiteral("qrz-key"), k) && k.isEmpty()); // the caller's copy is wiped
        CHECK(SecretStore::save(QStringLiteral("tqsl-password"), p) && p.isEmpty());
    }

    MainWindow w(&settings);
    w.show();
    auto *client = w.findChild<TciClient *>();
    CHECK(waitFor([&] { return client->isReady(); }, 5000));
    pump(500);
    auto *up = w.findChild<LogUploader *>();
    CHECK(up != nullptr);
    if (!up)
        return;
    auto *qso = w.findChild<QsoLogDock *>();
    const auto logQso = [&](const QString &call)
    {
        qso->setCall(call);
        for (QPushButton *b : qso->findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Log QSO"))
                b->click();
    };
    logQso(QStringLiteral("W1AW"));

    // GridTracker: the ADIF record in one datagram
    CHECK(waitFor([&] { return gridTracker.hasPendingDatagrams(); }, 3000));
    QByteArray d(int(gridTracker.pendingDatagramSize()), 0);
    gridTracker.readDatagram(d.data(), d.size());
    std::printf("  UDP: %s", d.constData());
    CHECK(d.contains("<CALL:4>W1AW") && d.contains("<EOR>") && d.contains("<STATION_CALLSIGN:6>KC9OXP"));

    // QRZ: INSERT with the key, everything encoded
    CHECK(waitFor([&] { return qrz.bodies.size() == 1 && up->qrzWaiting() == 0; }, 5000));
    if (!qrz.bodies.isEmpty())
    {
        const QUrlQuery body(qrz.bodies.first());
        CHECK(body.queryItemValue(QStringLiteral("KEY")) == keyText);
        CHECK(body.queryItemValue(QStringLiteral("ACTION")) == QStringLiteral("INSERT"));
        CHECK(body.queryItemValue(QStringLiteral("ADIF"), QUrl::FullyDecoded).contains(QStringLiteral("<CALL:4>W1AW")));
        CHECK(!qrz.bodies.first().contains(QLatin1Char('<'))); // encoded, nothing raw
    }
    CHECK(up->qrzState() == LogUploader::State::Ok);

    // LoTW: TQSL run with the password as one argument, the station location, the file
    if (!fake.isEmpty())
    {
        CHECK(waitFor([&] { return up->lotwWaiting() == 0 && up->lotwState() == LogUploader::State::Ok; }, 10000));
        QFile f(tqslLog);
        QString t = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        t.remove(QLatin1Char('\r')); // line endings as written on Windows
        std::printf("  fake TQSL saw:\n%s", qPrintable(t.left(600)));
        CHECK(t.contains(QStringLiteral("arg: -p\narg: ") + pwText + QLatin1Char('\n')));
        CHECK(t.contains(QStringLiteral("arg: -l\narg: Home EN71ar\n")));
        CHECK(t.contains(QStringLiteral("arg: -u\n")) && t.contains(QStringLiteral("arg: -x\n")));
        CHECK(t.contains(QStringLiteral("<CALL:4>W1AW")));
    }

    // Secrets never in the TCI log
    QPlainTextEdit *tciLog = nullptr;
    for (QDockWidget *dock : w.findChildren<QDockWidget *>())
        if (dock->objectName() == QStringLiteral("tciLog"))
            tciLog = dock->findChild<QPlainTextEdit *>();
    CHECK(tciLog != nullptr);
    if (tciLog)
    {
        const QString t = tciLog->toPlainText();
        CHECK(!t.contains(keyText) && !t.contains(QStringLiteral("pa ss")));
        if (!fake.isEmpty())
            CHECK(t.contains(QStringLiteral("-p ********")));
    }

    // QRZ refuses the key: stops, keeps the QSO; Test key passes again and it goes.
    qrz.answers << QStringLiteral("RESULT=AUTH&REASON=insufficient privileges");
    logQso(QStringLiteral("K3WW"));
    CHECK(waitFor([&] { return up->qrzState() == LogUploader::State::Problem; }, 5000));
    CHECK(up->qrzWaiting() == 1);
    logQso(QStringLiteral("W6EL")); // stopped: no request with a refused key
    pump(500);
    CHECK(qrz.bodies.size() == 2 && up->qrzWaiting() == 2);
    bool passed = false;
    QString passText;
    QObject::connect(up,
                     &LogUploader::qrzTestResult,
                     up,
                     [&](bool p, const QString &t)
                     {
                         passed = p;
                         passText = t;
                     });
    qrz.answers << QStringLiteral("RESULT=OK&DATA=CALLSIGN=KC9OXP&amp;COUNT=42");
    up->testQrz();
    CHECK(waitFor([&] { return !passText.isEmpty(); }, 5000));
    std::printf("  Test key: %s\n", qPrintable(passText));
    CHECK(passed && passText.contains(QStringLiteral("KC9OXP")) && passText.contains(QStringLiteral("42")));
    CHECK(QUrlQuery(qrz.bodies.at(2)).queryItemValue(QStringLiteral("ACTION")) == QStringLiteral("STATUS"));
    CHECK(waitFor([&] { return up->qrzWaiting() == 0; }, 5000)); // a good key: the two waiting went
    qrz.answers << QStringLiteral("RESULT=FAIL&REASON=invalid api key");
    passText.clear();
    up->testQrz();
    CHECK(waitFor([&] { return !passText.isEmpty(); }, 5000));
    CHECK(!passed && passText.startsWith(QStringLiteral("Fail")));

    // LoTW: no internet -> waits; a TQSL error -> stops with the reason
    if (!fake.isEmpty())
    {
        qputenv("FAKE_TQSL_STATUS", "11");
        logQso(QStringLiteral("N1MM"));
        CHECK(waitFor([&] { return up->lotwState() == LogUploader::State::Waiting; }, 10000));
        CHECK(up->lotwWaiting() == 1 && up->lotwText().contains(QStringLiteral("reach")));
        qputenv("FAKE_TQSL_STATUS", "4");
        up->retryNow();
        CHECK(waitFor([&] { return up->lotwState() == LogUploader::State::Problem; }, 10000));
        std::printf("  LoTW after status 4: %s\n", qPrintable(up->lotwText()));
        qputenv("FAKE_TQSL_STATUS", "0");
        up->retryNow();
        CHECK(waitFor([&] { return up->lotwWaiting() == 0 && up->lotwState() == LogUploader::State::Ok; }, 10000));
    }

    // a QSO whose call is not a call sign: ADIF file only
    const int before = qrz.bodies.size();
    logQso(QStringLiteral("TEST TEST"));
    pump(500);
    CHECK(qrz.bodies.size() == before);

    // status bar
    QPushButton *qrzBtn = nullptr;
    for (QPushButton *b : w.findChildren<QPushButton *>())
        if (b->text().startsWith(QStringLiteral("QRZ")))
            qrzBtn = b;
    CHECK(qrzBtn && qrzBtn->isVisible());
    SecretStore::forget(QStringLiteral("qrz-key"));
    SecretStore::forget(QStringLiteral("tqsl-password"));
    CHECK(!SecretStore::has(QStringLiteral("qrz-key")));
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
    runMemories();
    runPskReporterAndFreqBox();
    runGridTracker();
    runUploads();
    const int screens = args.indexOf(QStringLiteral("--screens"));
    if (screens >= 0)
        renderScreens(args.value(screens + 1, QStringLiteral(".")));
    std::printf(g_fail ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
