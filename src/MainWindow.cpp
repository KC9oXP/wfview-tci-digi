#include "MainWindow.h"

#include "AmtorEngine.h"
#include "AmtorPanel.h"
#include "AppPaths.h"
#include "AudioHub.h"
#include "CallLookup.h"
#include "CwDecoder.h"
#include "CwPanel.h"
#include "CwTxEngine.h"
#include "FreqEdit.h"
#include "HellEngine.h"
#include "HellPanel.h"
#include "LogUploader.h"
#include "GridTrackerLink.h"
#include "InputLevelMeter.h"
#include "MemoriesDialog.h"
#include "NavtexEngine.h"
#include "NavtexPanel.h"
#include "OliviaEngine.h"
#include "OliviaPanel.h"
#include "PskEngine.h"
#include "PskPanel.h"
#include "PskReporter.h"
#include "QsoLog.h"
#include "RadioModes.h"
#include "RttyEngine.h"
#include "RttyPanel.h"
#include "RxText.h"
#include "SecretStore.h"
#include "SessionLog.h"
#include "SpectrumEngine.h"
#include "SstvEngine.h"
#include "SstvPanel.h"
#include "TxPowerWatch.h"
#include "TxTools.h"
#include "UiStyle.h"
#include "UpdateCheck.h"
#include "WaterfallWidget.h"
#include "WefaxEngine.h"
#include "WefaxPanel.h"
#include "WwvEngine.h"
#include "WwvPanel.h"
#include "modemtxsource.h"
#include "rttytxsource.h"
#include "signalreport.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleFactory>
#include <QEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdlib>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

namespace
{
constexpr int reconnectMs = 5000;

QString formatFreq(qint64 hz)
{
    // 14074000 -> "14.074.000"
    QString s = QString::number(hz);
    for (int i = s.size() - 3; i > 0; i -= 3)
        s.insert(i, QLatin1Char('.'));
    return s;
}

qint64 parseFreq(const QString &text)
{
    // Accepts "14.074.000", "14074000", "14.074" (MHz), "14074 k".
    QString t = text.trimmed().toLower().remove(QLatin1Char(' ')).remove(QLatin1Char(','));
    double mult = 1.0;
    if (t.endsWith(QLatin1String("mhz")) || t.endsWith(QLatin1Char('m')))
    {
        mult = 1e6;
        t.remove(QRegularExpression(QStringLiteral("m(hz)?$")));
    }
    else if (t.endsWith(QLatin1String("khz")) || t.endsWith(QLatin1Char('k')))
    {
        mult = 1e3;
        t.remove(QRegularExpression(QStringLiteral("k(hz)?$")));
    }
    if (t.count(QLatin1Char('.')) > 1)
        return t.remove(QLatin1Char('.')).toLongLong();
    bool ok = false;
    const double v = t.toDouble(&ok);
    if (!ok || v <= 0)
        return 0;
    if (mult == 1.0 && v < 1000.0)
        mult = 1e6; // a bare "14.074" means MHz
    return qRound64(v * mult);
}
} // namespace

namespace
{
// Calls fn when the mouse button is released over the watched widget.
class ReleaseWatcher : public QObject
{
public:
    ReleaseWatcher(std::function<void()> fn, QObject *parent) : QObject(parent), m_fn(std::move(fn))
    {
    }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (ev->type() == QEvent::MouseButtonRelease)
            m_fn();
        return QObject::eventFilter(obj, ev);
    }

private:
    std::function<void()> m_fn;
};
} // namespace

MainWindow::MainWindow(QSettings *settings, QWidget *parent)
    : QMainWindow(parent)
    , m_settings(settings)
{
    AppPaths::init(m_settings);
    setWindowTitle(tr("wfview TCI Digi %1").arg(QStringLiteral(APP_VERSION)));

    m_client = new TciClient(this);
    m_cw = new CwDecoder(this);
    m_rtty = new RttyEngine(this);
    m_rtty->loadSettings(m_settings);
    m_psk = new PskEngine(this);
    m_psk->loadSettings(m_settings);
    m_hell = new HellEngine(this);
    m_hell->loadSettings(m_settings);
    m_cwTx = new CwTxEngine(this);
    m_cwTx->loadSettings(m_settings);
    m_olivia = new OliviaEngine(this);
    m_olivia->loadSettings(m_settings);
    m_sstv = new SstvEngine(this);
    m_sstv->loadSettings(m_settings);
    m_navtex = new NavtexEngine(m_settings, this);
    m_wefax = new WefaxEngine(m_settings, this);
    m_wwv = new WwvEngine(m_settings, this);
    m_spectrum = new SpectrumEngine(this);
    m_audio = new AudioHub(this);
    m_audio->setRingSeconds(m_settings->value(QStringLiteral("Audio/RingSeconds"), 120).toInt());
    m_session = new SessionLog(m_settings, this);
    m_amtor = new AmtorEngine(this);
    m_amtor->loadSettings(m_settings);
    m_modems = {m_psk, m_hell, m_cwTx, m_olivia, m_sstv, m_amtor};
    // Tune tone and CW ID: created before the tabs, which put their buttons in.
    m_tools = new TxTools(m_settings, this);
    m_txEngines = m_modems;
    m_txEngines << m_tools->tuneEngine() << m_tools->cwIdEngine();
    m_txLimitMin = std::clamp(m_settings->value(QStringLiteral("Tx/LimitMinutes"), 5).toInt(), 0, 60);
    m_powerWatch = new TxPowerWatch(this);
    m_powerWatch->setAlcLimit(m_settings->value(QStringLiteral("Tx/AlcWarn"), 1.0).toDouble());

    // TX audio comes from whichever mode is transmitting (only one can: see
    // onTransmitRequested). RTTY renders silence when idle.
    std::shared_ptr<RttyTxSource> rttyTx = m_rtty->txSource();
    std::vector<std::shared_ptr<ModemTxSource>> modemTx;
    for (ModemEngine *e : m_txEngines)
        modemTx.push_back(e->txSource());
    m_client->setTxRender(
        [rttyTx, modemTx](float *out, size_t frames, int channels, float rate)
        {
            for (const std::shared_ptr<ModemTxSource> &t : modemTx)
                if (t->active())
                {
                    t->render(out, frames, channels, rate);
                    return;
                }
            rttyTx->render(out, frames, channels, rate);
        });

    auto *central = new QWidget();
    auto *v = new QVBoxLayout(central);
    v->addWidget(buildConnectionBar());
    v->addWidget(buildTxHint());

    // Transmit-capable modes on the left, then a "Read only" divider and the
    // receive-only modes, so it is clear which tabs can transmit.
    m_tabs = new QTabWidget();
    m_cwPanel = new CwPanel(m_cw, m_cwTx, m_settings);
    m_rttyPanel = new RttyPanel(m_rtty, m_settings);
    m_pskPanel = new PskPanel(m_psk, m_settings);
    m_hellPanel = new HellPanel(m_hell, m_settings);
    m_oliviaPanel = new OliviaPanel(m_olivia, m_settings);
    m_sstvPanel = new SstvPanel(m_sstv, m_settings);
    m_amtorPanel = new AmtorPanel(m_amtor, m_settings);
    m_navtexPanel = new NavtexPanel(m_navtex);
    m_wefaxPanel = new WefaxPanel(m_wefax);
    m_tabs->addTab(m_cwPanel, tr("CW"));
    m_tabs->addTab(m_rttyPanel, tr("RTTY"));
    m_tabs->addTab(m_pskPanel, tr("PSK"));
    m_tabs->addTab(m_hellPanel, tr("Hell"));
    m_tabs->addTab(m_oliviaPanel, tr("Olivia"));
    m_tabs->addTab(m_sstvPanel, tr("SSTV"));
    m_tabs->addTab(m_amtorPanel, tr("AMTOR"));
    {
        auto *divider = new QLabel(tr("NAVTEX, WEFAX and WWV Time (to the right) only receive."));
        divider->setAlignment(Qt::AlignCenter);
        const int i = m_tabs->addTab(divider, QStringLiteral("    %1 \u25B8").arg(tr("Read only")));
        m_tabs->setTabEnabled(i, false);
        m_tabs->setTabToolTip(i,
                              tr("The tabs to the right of here only receive.\nDrag tabs to put them in your own "
                                 "order (transmit tabs stay left of here, receive-only tabs right); "
                                 "View > Reset tab order puts them back."));
        m_tabDivider = divider;
    }
    m_tabs->addTab(m_navtexPanel, tr("NAVTEX"));
    m_tabs->addTab(m_wefaxPanel, tr("WEFAX"));
    m_wwvPanel = new WwvPanel(m_wwv);
    m_tabs->addTab(m_wwvPanel, tr("WWV Time"));
    {
        // Stable keys for the saved order (tab texts are translated); tx = can transmit.
        const struct
        {
            QWidget *w;
            const char *key;
            bool tx;
        } keys[] = {{m_cwPanel, "CW", true},
                    {m_rttyPanel, "RTTY", true},
                    {m_pskPanel, "PSK", true},
                    {m_hellPanel, "Hell", true},
                    {m_oliviaPanel, "Olivia", true},
                    {m_sstvPanel, "SSTV", true},
                    {m_amtorPanel, "AMTOR", true},
                    {m_tabDivider, "ReadOnly", false},
                    {m_navtexPanel, "NAVTEX", false},
                    {m_wefaxPanel, "WEFAX", false},
                    {m_wwvPanel, "WWV", false}};
        for (const auto &k : keys)
        {
            k.w->setProperty("tabKey", QString::fromLatin1(k.key));
            k.w->setProperty("tabTx", k.tx);
        }
    }
    m_tabs->setMovable(true);
    arrangeTabs(m_settings->value(QStringLiteral("Window/TabOrder")).toStringList());
    connect(m_tabs->tabBar(),
            &QTabBar::tabMoved,
            this,
            [this](int, int)
            {
                if (!m_arrangingTabs)
                    m_tabsDragged = true;
            });
    // After the drop (not during the drag): put each tab back on its side of "Read only", save.
    m_tabs->tabBar()->installEventFilter(new ReleaseWatcher(
        [this]()
        {
            if (!m_tabsDragged)
                return;
            m_tabsDragged = false;
            QTimer::singleShot(0, this, &MainWindow::tabsMoved);
        },
        this));
    v->addWidget(m_tabs, 1);
    setCentralWidget(central);

    m_diag = new QLabel(tr("Not connected"));
    m_diag->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_diag, 1);
    m_audioState = new QLabel();
    statusBar()->addPermanentWidget(m_audioState);
    m_pskr = new PskReporter(m_settings, this);
    m_pskrBtn = new QPushButton(tr("PSKR"));
    m_pskrBtn->setFlat(true);
    m_pskrBtn->setFocusPolicy(Qt::NoFocus);
    m_pskrBtn->setCursor(Qt::PointingHandCursor);
    statusBar()->addPermanentWidget(m_pskrBtn);
    m_uploader = new LogUploader(m_settings, this);
    m_gridTracker = new GridTrackerLink(m_settings, this);
    m_gridTracker->setObjectName(QStringLiteral("gridTracker"));
    connect(m_gridTracker, &GridTrackerLink::message, this, &MainWindow::log);
    m_qrzBtn = new QPushButton(tr("QRZ"));
    m_lotwBtn = new QPushButton(tr("LoTW"));
    for (QPushButton *b : {m_qrzBtn, m_lotwBtn})
    {
        b->setFlat(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        statusBar()->addPermanentWidget(b);
        connect(b, &QPushButton::clicked, this, [this]() { m_uploader->editSettings(this); });
    }
    connect(m_uploader, &LogUploader::statusChanged, this, &MainWindow::updateUploadButtons);
    connect(m_uploader, &LogUploader::message, this, &MainWindow::log);
    connect(m_pskrBtn, &QPushButton::clicked, this, &MainWindow::togglePskReporter);
    m_pskrBtn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_pskrBtn, &QWidget::customContextMenuRequested, this, [this](const QPoint &) { editPskReporter(); });

    buildLogDock();
    m_update = new UpdateCheck(m_settings, this);
    connect(m_update, &UpdateCheck::logLine, this, &MainWindow::log);
    connect(m_update,
            &UpdateCheck::noticeShown,
            this,
            [this](const QString &v)
            {
                statusBar()->showMessage(
                    tr("Version %1 is available: click the amber Update badge (bottom right).").arg(v), 20000);
            });
    m_qso = new QsoLogDock(m_settings, this);
    m_lookup = new CallLookup(m_settings, this);
    m_qso->setLookup(m_lookup);
    m_qso->setSignalNote([this]() { return signalNote(); });
    addDockWidget(Qt::RightDockWidgetArea, m_qso);
    m_qso->hide();
    buildFileMenu();
    buildMenus();

    // ---- wiring
    connect(m_client, &TciClient::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_client, &TciClient::radioInfoChanged, this, &MainWindow::onRadioInfo);
    connect(m_client, &TciClient::frequencyChanged, this, &MainWindow::onFrequency);
    connect(m_client, &TciClient::modulationChanged, this, &MainWindow::onModulation);
    connect(m_client, &TciClient::radioTransmitChanged, this, &MainWindow::onRadioTransmit);
    // Receive audio goes through the hub (ring buffer, recording, WAV playback).
    connect(m_client, &TciClient::rxAudio, m_audio, &AudioHub::fromRadio);
    connect(m_client,
            &TciClient::rxAudio,
            this,
            [this](const QVector<float> &mono, float) { m_inMeter.add(mono.constData(), size_t(mono.size())); });
    connect(m_audio, &AudioHub::audio, m_cw, &CwDecoder::pushAudio);
    connect(m_audio, &AudioHub::audio, m_rtty, &RttyEngine::pushAudio);
    connect(m_audio, &AudioHub::audio, m_psk, &ModemEngine::pushAudio);
    connect(m_audio, &AudioHub::audio, m_hell, &ModemEngine::pushAudio);
    connect(m_audio, &AudioHub::audio, m_olivia, &ModemEngine::pushAudio);
    connect(m_audio, &AudioHub::audio, m_sstv, &ModemEngine::pushAudio);
    connect(m_audio, &AudioHub::audio, m_amtor, &ModemEngine::pushAudio);
    connect(m_audio, &AudioHub::audio, m_navtex, &NavtexEngine::pushAudio);
    connect(m_audio, &AudioHub::audio, m_wefax, &WefaxEngine::pushAudio);
    connect(m_audio, &AudioHub::audio, m_wwv, &WwvEngine::pushAudio);
    connect(m_audio, &AudioHub::audio, m_spectrum, &SpectrumEngine::pushAudio);
    connect(m_spectrum, &SpectrumEngine::line, this, &MainWindow::trackSnr);
    m_snrShow.setInterval(300);
    connect(&m_snrShow, &QTimer::timeout, this, &MainWindow::updateSnrLabel);
    connect(&m_snrShow, &QTimer::timeout, this, &MainWindow::updateInputLevel);
    connect(&m_snrShow, &QTimer::timeout, this, &MainWindow::updateGridTracker);
    m_snrShow.start();
    connect(m_audio,
            &AudioHub::playbackChanged,
            this,
            [this](bool playing, double)
            {
                m_wwv->setLive(!playing); // a recording says nothing about the PC clock
                updateAudioStatus();
            });
    connect(m_audio, &AudioHub::recordingChanged, this, [this](bool, double) { updateAudioStatus(); });
    connect(m_spectrum, &SpectrumEngine::line, m_pskPanel, &ModemPanel::addSpectrumLine);
    connect(m_spectrum, &SpectrumEngine::line, m_hellPanel, &ModemPanel::addSpectrumLine);
    connect(m_spectrum, &SpectrumEngine::line, m_oliviaPanel, &ModemPanel::addSpectrumLine);
    connect(m_spectrum, &SpectrumEngine::line, m_amtorPanel, &ModemPanel::addSpectrumLine);
    connect(m_spectrum, &SpectrumEngine::line, m_navtexPanel, &NavtexPanel::addSpectrumLine);
    connect(m_spectrum, &SpectrumEngine::line, m_sstvPanel, &SstvPanel::addSpectrumLine);
    connect(m_spectrum, &SpectrumEngine::line, m_wefaxPanel, &WefaxPanel::addSpectrumLine);
    connect(m_spectrum, &SpectrumEngine::line, m_wwvPanel, &WwvPanel::addSpectrumLine);
    // WWV Time: the audio clock error goes to the SSTV and WEFAX trims; a carrier
    // measurement borrows the radio for a few seconds.
    connect(m_wwvPanel,
            &WwvPanel::trimPpmChosen,
            this,
            [this](double ppm)
            {
                m_sstv->setSlantPpm(std::round(ppm));
                m_wefax->setSlantPpm(std::round(ppm));
                statusBar()->showMessage(tr("SSTV and WEFAX trim set to %1 ppm.").arg(qRound(ppm)), 6000);
            });
    m_wwvPanel->setTrimSource(
        [this](int &sstv, int &wefax)
        {
            sstv = qRound(m_sstv->slantPpm());
            wefax = qRound(m_wefax->slantPpm());
        });
    connect(m_wwv,
            &WwvEngine::tuneForCarrier,
            this,
            [this](qint64 dialHz, bool lsb)
            {
                // plain USB / LSB (not DIGU / DIGL: wfview would change the radio's data mode)
                QString ssb = lsb ? QStringLiteral("lsb") : QStringLiteral("usb");
                for (const QString &m : m_client->modulations())
                    if (RadioModes::kind(m) == RadioModes::Kind::Voice && RadioModes::isLsb(m) == lsb)
                    {
                        ssb = m;
                        break;
                    }
                if (RadioModes::normalised(m_client->modulation()) != RadioModes::normalised(ssb))
                    m_client->setModulation(ssb);
                m_client->setFrequency(dialHz);
                log(tr("WWV carrier measurement: %1 Hz %2").arg(formatFreq(dialHz), ssb.toUpper()));
            });
    connect(m_wwvPanel,
            &WwvPanel::tuneRequested,
            this,
            [this](qint64 hz)
            {
                QString am = QStringLiteral("am");
                for (const QString &m : m_client->modulations())
                    if (RadioModes::normalised(m) == QLatin1String("AM"))
                    {
                        am = m;
                        break;
                    }
                emit m_wwv->restoreRadio(hz, am);
            });
    connect(
        m_wwv,
        &WwvEngine::restoreRadio,
        this,
        [this](qint64 hz, const QString &mode)
        {
            // Mode first, then the frequency: on the FT-991A a mode change after the
            // frequency moved the dial (it came back 700 Hz low). Then check what the
            // radio reports and send the frequency again if it is not there.
            if (!mode.isEmpty() && RadioModes::normalised(m_client->modulation()) != RadioModes::normalised(mode))
                m_client->setModulation(mode);
            m_client->setFrequency(hz);
            log(tr("WWV: radio to %1 Hz %2").arg(formatFreq(hz), mode));
            auto check = [this, hz, mode](int tries, auto &&self) -> void
            {
                QTimer::singleShot(
                    1500,
                    this,
                    [this, hz, mode, tries, self]()
                    {
                        if (!m_client->isReady())
                            return;
                        const bool modeOk = mode.isEmpty()
                            || RadioModes::normalised(m_client->modulation()) == RadioModes::normalised(mode);
                        if (m_client->frequencyHz() == hz && modeOk)
                            return;
                        if (tries <= 0)
                        {
                            log(tr("WWV: the radio did not go back to %1 Hz %2 (it reports %3 Hz %4).")
                                    .arg(formatFreq(hz),
                                         mode,
                                         formatFreq(m_client->frequencyHz()),
                                         m_client->modulation()));
                            return;
                        }
                        log(tr("WWV: radio reports %1 Hz %2, sending %3 Hz %4 again.")
                                .arg(
                                    formatFreq(m_client->frequencyHz()), m_client->modulation(), formatFreq(hz), mode));
                        if (!modeOk)
                            m_client->setModulation(mode);
                        m_client->setFrequency(hz);
                        self(tries - 1, self);
                    });
            };
            check(2, check);
        });
    for (WaterfallWidget *wf : findChildren<WaterfallWidget *>())
        connect(wf, &WaterfallWidget::saveRequested, this, [this, wf]() { saveWaterfallShot(wf); });
    connect(m_client, &TciClient::diagnostics, m_diag, &QLabel::setText);
    connect(m_client, &TciClient::txSensors, m_powerWatch, &TxPowerWatch::onSensors);
    connect(m_client, &TciClient::txSensors, m_tools, &TxTools::onTxSensors);
    connect(m_client, &TciClient::radioTransmitChanged, m_tools, &TxTools::setRadioTransmitting);
    connect(m_client, &TciClient::radioTransmitChanged, m_powerWatch, &TxPowerWatch::setRadioTransmitting);
    connect(m_powerWatch, &TxPowerWatch::readout, m_txMeter, &QLabel::setText);
    connect(m_powerWatch,
            &TxPowerWatch::noPower,
            this,
            [this]()
            {
                log(tr("Radio reported no output power while transmitting (normal at very low power settings; "
                       "otherwise check its data mode and DATA MOD input, LAN/USB)."));
                if (!m_hintDismissed)
                    m_txHint->show();
            });
    connect(m_powerWatch, &TxPowerWatch::powerSeen, m_txHint, &QWidget::hide);
    connect(m_powerWatch,
            &TxPowerWatch::alcHigh,
            this,
            [this](double alc)
            {
                const QString t = tr("ALC is high (%1): lower the TX level of this mode (or wfview's TX gain). "
                                     "Data modes want little or no ALC.")
                                      .arg(alc, 0, 'f', 1);
                log(t);
                statusBar()->showMessage(t, 10000);
            });
    connect(m_powerWatch,
            &TxPowerWatch::alcChanged,
            this,
            [this](bool high)
            {
                m_txMeter->setStyleSheet(high ? QStringLiteral("background-color: #b03a10; color: white;"
                                                               " font-weight: bold; padding: 0 4px;")
                                              : QString());
                m_txMeter->setToolTip(high ? tr("ALC high: lower the TX level.")
                                           : tr("Output power, ALC and SWR as wfview reports them while "
                                                "transmitting."));
            });
    connect(m_client, &TciClient::logLine, this, &MainWindow::log);
    connect(m_rtty, &RttyEngine::transmitRequested, this, [this](bool on) { onTransmitRequested(m_rtty, on); });
    for (ModemEngine *e : m_txEngines)
        connect(e, &ModemEngine::transmitRequested, this, [this, e](bool on) { onTransmitRequested(e, on); });
    // CW ID after a transmission ({CWID}, SSTV picture): direct connection, so
    // it keys up before the mode lets go of PTT.
    connect(m_rtty, &RttyEngine::cwIdRequested, m_tools, &TxTools::sendCwId);
    for (ModemEngine *e : m_modems)
        connect(e, &ModemEngine::cwIdRequested, m_tools, &TxTools::sendCwId);
    connect(m_tools, &TxTools::message, this, [this](const QString &t) { statusBar()->showMessage(t, 8000); });
    connect(m_tools, &TxTools::message, this, &MainWindow::log);
    connect(m_tools,
            &TxTools::autoLevelProblem,
            this,
            [this](const QString &t)
            {
                // Not modal: the result stays on screen until read, nothing waits on it.
                auto *box = new QMessageBox(QMessageBox::Warning, tr("Auto TX level"), t, QMessageBox::Ok, this);
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->open();
            });

    // Transmit time limit: everything stops after this long keyed.
    m_txLimit.setSingleShot(true);
    connect(&m_txLimit,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (!anyTransmitting())
                    return;
                const QString t = tr("Transmit time limit (%1 min) reached: transmitter released. "
                                     "Options > TX time limit.")
                                      .arg(m_txLimitMin);
                abortAllTx(t);
                log(t);
                statusBar()->showMessage(t, 15000);
            });

    // One "My call" for every mode.
    const auto shareCall = [this](const QString &c)
    {
        m_rtty->setMyCall(c);
        for (ModemEngine *e : m_modems)
            e->setMyCall(c);
        m_tools->cwIdEngine()->setMyCall(c);
    };
    // Start from the shared value (the new modes read Station/MyCall, falling back to 0.1.0's RTTY/MyCall).
    shareCall(m_psk->myCall().isEmpty() ? m_rtty->myCall() : m_psk->myCall());
    connect(m_rtty, &RttyEngine::settingsChanged, this, [this, shareCall]() { shareCall(m_rtty->myCall()); });
    for (ModemEngine *e : m_modems)
        connect(e, &ModemEngine::settingsChanged, this, [e, shareCall]() { shareCall(e->myCall()); });

    wireLogs();

    m_reconnect.setInterval(reconnectMs);
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (m_wantConnected && m_client->state() == TciClient::State::Disconnected)
                    m_client->connectToServer(m_host->text(), quint16(m_port->value()));
            });

    // ---- restore
    m_settings->beginGroup(QStringLiteral("Window"));
    if (!restoreGeometry(m_settings->value(QStringLiteral("Geometry")).toByteArray()))
    {
        // First start: big enough for the tallest tab (SSTV), within the screen.
        const QRect avail = screen() ? screen()->availableGeometry() : QRect(0, 0, 1280, 900);
        resize(std::min(1280, avail.width() * 9 / 10), std::min(900, avail.height() * 9 / 10));
    }
    restoreState(m_settings->value(QStringLiteral("State")).toByteArray());
    {
        // A window saved shorter than its contents need (older versions) grows to fit,
        // so no tab gets squashed; never past the screen.
        const int need = minimumSizeHint().height();
        const int room = screen() ? screen()->availableGeometry().height() : need;
        if (!isMaximized() && height() < need)
            resize(width(), std::min(need, room));
    }
    {
        // Saved by tab name since 0.4.1 (the order changed).
        const QString name = m_settings->value(QStringLiteral("TabName")).toString();
        int idx = 0;
        for (int i = 0; i < m_tabs->count(); ++i)
            if (m_tabs->isTabEnabled(i) && m_tabs->tabText(i) == name)
                idx = i;
        m_tabs->setCurrentIndex(idx);
    }
    m_settings->endGroup();
    connect(m_tabs,
            &QTabWidget::currentChanged,
            this,
            [this](int)
            {
                updateQsoMode();
                fillPresetBox();
                if (m_memories)
                    m_memories->setCurrentTab(currentTabKey());
            });
    updateQsoMode();
    fillPresetBox();
    if (m_settings->value(QStringLiteral("TCI/ConnectOnStart"), false).toBool())
        QTimer::singleShot(0, this, &MainWindow::toggleConnection);

    updateRadioState();
    m_update->startAutomatic();
}

MainWindow::~MainWindow() = default;

QWidget *MainWindow::buildTxHint()
{
    m_txHint = new QWidget();
    m_txHint->setObjectName(QStringLiteral("txHint"));
    m_txHint->setStyleSheet(QStringLiteral("#txHint { background-color: #3e3420; border-radius: 3px; }"));
    auto *h = new QHBoxLayout(m_txHint);
    h->setContentsMargins(8, 4, 4, 4);
    auto *text = new QLabel(
        tr("<b>Hint: the radio reported no output power while transmitting.</b> At very low power settings "
           "this can be normal. Otherwise the transmit audio may not be reaching the input the radio uses: "
           "check that its <b>data mode</b> (Data 1/2/3 in wfview) takes the <b>DATA MOD input</b> wfview feeds, "
           "LAN or USB (IC-7610 with DATA1 = USB and DATA2 = LAN: use Data 2 over LAN), the TX level, and that "
           "the radio is in a data or SSB mode."));
    text->setWordWrap(true);
    text->setStyleSheet(QStringLiteral("color: #e8dcc0;"));
    h->addWidget(text, 1);
    auto *close = new QPushButton(QStringLiteral("\u2715"));
    close->setFlat(true);
    close->setToolTip(tr("Hide"));
    close->setFixedWidth(close->sizeHint().height());
    h->addWidget(close, 0, Qt::AlignTop);
    auto *never = new QPushButton(tr("Don't show again"));
    never->setFlat(true);
    never->setToolTip(tr("Hide this hint until the program is restarted."));
    h->addWidget(never, 0, Qt::AlignTop);
    connect(close, &QPushButton::clicked, m_txHint, &QWidget::hide);
    connect(never,
            &QPushButton::clicked,
            this,
            [this]()
            {
                m_hintDismissed = true;
                m_txHint->hide();
            });
    m_txHint->hide();
    return m_txHint;
}

QWidget *MainWindow::buildConnectionBar()
{
    auto *bar = new QWidget();
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(0, 0, 0, 0);

    h->addWidget(new QLabel(tr("wfview TCI")));
    m_host = new QLineEdit(m_settings->value(QStringLiteral("TCI/Host"), QStringLiteral("127.0.0.1")).toString());
    m_host->setToolTip(tr("Computer running wfview (127.0.0.1 = this computer)."));
    m_host->setMaximumWidth(180);
    h->addWidget(m_host);
    m_port = new QSpinBox();
    m_port->setRange(1, 65535);
    m_port->setValue(m_settings->value(QStringLiteral("TCI/Port"), 50001).toInt());
    m_port->setToolTip(tr("wfview's TCI server port (Settings > External Control > TCI Server Port, default 50001)."));
    h->addWidget(m_port);
    m_connect = new QPushButton(tr("Connect"));
    UiStyle::connectButton(m_connect, UiStyle::Link::Off);
    {
        // wide enough for "Disconnect" in bold, so the row does not shift
        QFont f = m_connect->font();
        f.setBold(true);
        m_connect->setMinimumWidth(QFontMetrics(f).horizontalAdvance(tr("Disconnect")) + 30);
    }
    h->addWidget(m_connect);
    m_autoReconnect = new QCheckBox(tr("Reconnect"));
    m_autoReconnect->setToolTip(tr("Reconnect automatically if the connection drops."));
    m_autoReconnect->setChecked(m_settings->value(QStringLiteral("TCI/AutoReconnect"), true).toBool());
    h->addWidget(m_autoReconnect);
    m_connState = new QLabel();
    m_connState->setMinimumWidth(40);
    // Long messages are clipped rather than widening the window; the tooltip has them in full.
    m_connState->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    h->addWidget(m_connState, 1);

    m_logBtn = new QPushButton(tr("Log"));
    UiStyle::greenWhenOn(m_logBtn);
    m_logBtn->setToolTip(tr("Session log on (green) / off: what every mode decodes and sends goes to a text file "
                            "per mode per day in the logs folder. One setting for all modes, kept across "
                            "restarts."));
    h->addWidget(m_logBtn);
    m_presetBox = new QComboBox();
    m_presetBox->setToolTip(tr("Frequency presets for this tab: sets the dial (and USB/LSB) over TCI."));
    m_presetBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_presetBox->setMinimumContentsLength(8);
    h->addWidget(m_presetBox);
    connect(m_presetBox,
            &QComboBox::activated,
            this,
            [this](int i)
            {
                const QVariant d = m_presetBox->itemData(i);
                const QString kind = d.toString();
                const int index = m_presetBox->itemData(i, Qt::UserRole + 1).toInt();
                syncPresetBox(); // back to "Presets" until the dial is on a preset
                if (kind == QLatin1String("add"))
                    addCurrentPreset();
                else if (kind == QLatin1String("edit"))
                    editPresets();
                else if (kind == QLatin1String("preset"))
                {
                    const QList<Preset> list = presets(currentTabKey());
                    if (index >= 0 && index < list.size())
                        applyPreset(list.at(index));
                }
            });

    m_memBtn = new QPushButton(tr("Memories"));
    m_memBtn->setToolTip(tr("Memories (Ctrl+M): your list of frequencies with tab, sub-mode, sideband, license "
                            "classes and comments. Click one to go there. Kept in memories.csv in the data folder, "
                            "so it can be edited in Excel too."));
    h->addWidget(m_memBtn);
    connect(m_memBtn, &QPushButton::clicked, this, &MainWindow::showMemories);
    {
        auto *sc = new QShortcut(QKeySequence(tr("Ctrl+M")), this);
        connect(sc, &QShortcut::activated, this, &MainWindow::showMemories);
    }

    m_freq = new FreqEdit();
    m_freq->setToolTip(tr("Receiver frequency in Hz (all 9 digits, so you can step from 1.8 to 10 MHz in one "
                          "turn). Over a digit: mouse wheel steps it, click the top half to step up, the bottom "
                          "half to step down.\nTo type one (e.g. 14.080 or 14080k): right-click > Type a "
                          "frequency..., type and press Enter (Esc cancels)."));
    QFont ff = m_freq->font();
    ff.setPointSizeF(ff.pointSizeF() * 1.3);
    ff.setBold(true);
    m_freq->setFont(ff);
    m_freq->setMinimumWidth(QFontMetrics(ff).horizontalAdvance(QStringLiteral("888.888.888")) + 16);
    m_freq->setMaximumWidth(QFontMetrics(ff).horizontalAdvance(QStringLiteral("888.888.888")) + 24);
    h->addWidget(m_freq);
    m_mode = new QComboBox();
    m_mode->setToolTip(
        tr("Radio mode. Every transmit mode here sends audio, so it needs a data mode (DIGU/DIGL) "
           "or USB/LSB.\nNote: choosing DIGU/DIGL here makes wfview use Data 1. If your radio's "
           "Data 1 input is not the one wfview feeds (LAN or USB), set the data mode in wfview instead."));
    m_mode->setMinimumContentsLength(5);
    h->addWidget(m_mode);
    m_snrLabel = new QLabel();
    m_snrLabel->setAlignment(Qt::AlignCenter);
    m_snrLabel->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("SNR -88 dB")) + 12);
    h->addWidget(m_snrLabel);
    m_inLevel = new InputLevelMeter();
    h->addWidget(m_inLevel);
    m_txLamp = new QLabel(tr(" RX "));
    m_txLamp->setAlignment(Qt::AlignCenter);
    m_txLamp->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("  TX  ")) + 8);
    h->addWidget(m_txLamp);
    m_txMeter = new QLabel();
    m_txMeter->setToolTip(tr("Output power, ALC and SWR as wfview reports them while transmitting."));
    h->addWidget(m_txMeter);

    connect(m_connect, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(m_freq,
            &QLineEdit::returnPressed,
            this,
            [this]()
            {
                const qint64 hz = parseFreq(m_freq->text());
                if (hz > 0)
                    m_client->setFrequency(hz);
                m_freq->clearFocus();
                if (hz > 0)
                    m_freq->setText(FreqEdit::format(hz)); // until the radio reports it
            });
    connect(m_freq, &FreqEdit::tuneRequested, m_client, &TciClient::setFrequency);
    connect(m_mode, &QComboBox::activated, this, [this](int) { m_client->setModulation(m_mode->currentText()); });
    return bar;
}

void MainWindow::buildLogDock()
{
    m_logDock = new QDockWidget(tr("TCI log"), this);
    m_logDock->setObjectName(QStringLiteral("tciLog"));
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    m_log = new QPlainTextEdit();
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    v->addWidget(m_log, 1);
    auto *row = new QHBoxLayout();
    auto *cmd = new QLineEdit();
    cmd->setPlaceholderText(tr("Raw TCI command, e.g. vfo:0,0;  or  modulation:0,digu;"));
    auto *send = new QPushButton(tr("Send"));
    row->addWidget(cmd, 1);
    row->addWidget(send);
    v->addLayout(row);
    m_logDock->setWidget(w);
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);
    m_logDock->hide();

    const auto sendCmd = [this, cmd]()
    {
        QString t = cmd->text().trimmed();
        if (t.isEmpty())
            return;
        if (!t.endsWith(QLatin1Char(';')))
            t += QLatin1Char(';');
        m_client->sendText(t);
        log(QStringLiteral("> ") + t);
        cmd->clear();
    };
    connect(send, &QPushButton::clicked, this, sendCmd);
    connect(cmd, &QLineEdit::returnPressed, this, sendCmd);
}

void MainWindow::buildMenus()
{
    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_logDock->toggleViewAction());
    QAction *qsoAct = m_qso->toggleViewAction();
    qsoAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q));
    view->addAction(qsoAct);
    QAction *dark = view->addAction(tr("Dark theme"));
    dark->setCheckable(true);
    dark->setChecked(m_settings->value(QStringLiteral("Window/Dark"), true).toBool());
    connect(dark,
            &QAction::toggled,
            this,
            [this](bool on)
            {
                m_settings->setValue(QStringLiteral("Window/Dark"), on);
                applyTheme(on);
            });
    applyTheme(dark->isChecked());
    view->addAction(tr("Reset tab order"),
                    this,
                    [this]()
                    {
                        m_settings->remove(QStringLiteral("Window/TabOrder"));
                        arrangeTabs({});
                    });

    QMenu *opts = menuBar()->addMenu(tr("&Options"));
    opts->setToolTipsVisible(true);

    // Language changes are saved immediately and applied on the next launch.
    // This avoids partially re-translating a complex UI while modem/TCI objects
    // and active receive/transmit sessions are running.
    QMenu *languageMenu = opts->addMenu(tr("Language"));
    auto *languageGroup = new QActionGroup(languageMenu);
    languageGroup->setExclusive(true);

    const QString selectedLanguage =
        m_settings->value(QStringLiteral("UI/Language"), QStringLiteral("system")).toString();

    auto addLanguageAction = [languageMenu, languageGroup, selectedLanguage](const QString &code,
                                                                            const QString &label)
    {
        QAction *action = languageMenu->addAction(label);
        action->setCheckable(true);
        action->setData(code);
        action->setChecked(selectedLanguage == code);
        languageGroup->addAction(action);
    };

    addLanguageAction(QStringLiteral("system"), tr("System language"));
    addLanguageAction(QStringLiteral("en"), QStringLiteral("English"));

    const QDir translationDir(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations")));
    const QStringList translationFiles =
        translationDir.entryList(QStringList() << QStringLiteral("wfview_tci_digi_*.qm"),
                                 QDir::Files,
                                 QDir::Name);
    for (const QString &file : translationFiles)
    {
        QString code = file;
        code.remove(QStringLiteral("wfview_tci_digi_"));
        code.chop(3); // ".qm"
        if (code.isEmpty() || code == QStringLiteral("en") || code == QStringLiteral("en_US"))
            continue;

        const QLocale locale(code);
        QString label = locale.nativeLanguageName();
        if (label.isEmpty())
            label = code;
        label += QStringLiteral(" [") + code + QStringLiteral("]");
        addLanguageAction(code, label);
    }

    connect(languageGroup, &QActionGroup::triggered, this, [this, languageGroup](QAction *action)
    {
        const QString newLanguage = action->data().toString();
        const QString oldLanguage =
            m_settings->value(QStringLiteral("UI/Language"), QStringLiteral("system")).toString();
        if (newLanguage == oldLanguage)
            return;

        const QString description =
            newLanguage == QStringLiteral("system")
                ? tr("the operating-system language")
                : (newLanguage == QStringLiteral("en") ? QStringLiteral("English") : newLanguage);

        const auto answer = QMessageBox::question(
            this,
            tr("Change language"),
            tr("Change the interface language to %1?\n\n"
               "The setting will be saved now and will take effect the next time you start wfview TCI Digi.")
                .arg(description),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);

        if (answer != QMessageBox::Yes)
        {
            for (QAction *item : languageGroup->actions())
                item->setChecked(item->data().toString() == oldLanguage);
            return;
        }

        m_settings->setValue(QStringLiteral("UI/Language"), newLanguage);
        m_settings->sync();
        QMessageBox::information(
            this,
            tr("Language"),
            tr("Language setting saved. Restart wfview TCI Digi to apply it."));
    });

    QAction *onStart = opts->addAction(tr("Connect on start"));
    onStart->setCheckable(true);
    onStart->setChecked(m_settings->value(QStringLiteral("TCI/ConnectOnStart"), false).toBool());
    connect(onStart,
            &QAction::toggled,
            this,
            [this](bool on) { m_settings->setValue(QStringLiteral("TCI/ConnectOnStart"), on); });
    QAction *sess = opts->addAction(tr("Session log (decoded text to a daily file per mode)"));
    sess->setCheckable(true);
    sess->setChecked(m_session->enabled());
    m_sessionAct = sess;
    m_logBtn->setChecked(m_session->enabled());
    // The Log button and this menu item are the same setting.
    connect(sess,
            &QAction::toggled,
            this,
            [this](bool on)
            {
                m_session->setEnabled(on);
                const QSignalBlocker b(m_logBtn);
                m_logBtn->setChecked(on);
            });
    connect(m_logBtn,
            &QPushButton::toggled,
            this,
            [this](bool on)
            {
                m_session->setEnabled(on);
                const QSignalBlocker b(m_sessionAct);
                m_sessionAct->setChecked(on);
                statusBar()->showMessage(
                    on ? tr("Session log on: %1").arg(QDir::toNativeSeparators(m_session->folder()))
                       : tr("Session log off."),
                    5000);
            });
    opts->addSeparator();
    QMenu *limit = opts->addMenu(tr("TX time limit"));
    limit->setToolTipsVisible(true);
    for (int min : {0, 1, 2, 3, 5, 10})
    {
        QAction *a = limit->addAction(min == 0 ? tr("Off") : tr("%n minute(s)", nullptr, min));
        a->setCheckable(true);
        a->setChecked(m_txLimitMin == min);
        a->setToolTip(tr("Release the transmitter after this long keyed (any mode, Tune too). The longest SSTV "
                         "modes (PD240, Scottie DX) take about 4.5 minutes."));
        connect(a,
                &QAction::triggered,
                this,
                [this, limit, a, min]()
                {
                    for (QAction *o : limit->actions())
                        o->setChecked(o == a);
                    m_txLimitMin = min;
                    m_settings->setValue(QStringLiteral("Tx/LimitMinutes"), min);
                    if (m_txLimitMin == 0)
                        m_txLimit.stop();
                });
    }
    opts->addAction(tr("Memories..."), this, &MainWindow::showMemories);
    opts->addAction(tr("GridTracker (live frequency and heard calls)..."),
                    this,
                    [this]() { m_gridTracker->editSettings(this); });
    opts->addAction(
        tr("Logging and uploads (GridTracker, QRZ, LoTW)..."), this, [this]() { m_uploader->editSettings(this); });
    opts->addAction(tr("Callsign lookup (QRZ / HamQTH)..."), this, &MainWindow::editCallLookup);
    opts->addAction(tr("ALC warning..."), this, &MainWindow::editAlcLimit);
    opts->addAction(tr("Tune tone, CW ID and Auto TX level..."), this, [this]() { m_tools->editSettings(this); });
    opts->addAction(tr("PSK Reporter..."), this, &MainWindow::editPskReporter);
    opts->addSeparator();
    opts->addAction(tr("Data folder..."),
                    this,
                    [this]()
                    {
                        const QString d = QFileDialog::getExistingDirectory(
                            this, tr("Folder for logs, pictures, screenshots and recordings"), AppPaths::base());
                        if (!d.isEmpty())
                        {
                            AppPaths::setBase(d);
                            log(tr("Data folder: %1").arg(QDir::toNativeSeparators(AppPaths::base())));
                        }
                    });
    QMenu *ring = opts->addMenu(tr("Audio kept for \"Save last...\""));
    for (int sec : {60, 120, 300, 600})
    {
        QAction *a = ring->addAction(tr("%1 minutes").arg(sec / 60.0, 0, 'g', 2));
        a->setCheckable(true);
        a->setChecked(m_audio->ringSeconds() == sec);
        connect(a,
                &QAction::triggered,
                this,
                [this, ring, a, sec]()
                {
                    for (QAction *o : ring->actions())
                        o->setChecked(o == a);
                    m_audio->setRingSeconds(sec);
                    m_settings->setValue(QStringLiteral("Audio/RingSeconds"), sec);
                });
    }
    QAction *autoUpdate = opts->addAction(tr("Check for updates at start-up"));
    autoUpdate->setCheckable(true);
    autoUpdate->setChecked(m_update->automatic());
    connect(autoUpdate, &QAction::toggled, m_update, &UpdateCheck::setAutomatic);
    QAction *egg = opts->addAction(tr("STANAG 4481 Easteregg"));
    egg->setCheckable(true);
    egg->setChecked(m_rtty->easterEgg());
    egg->setToolTip(tr("RTTY tab: when a 75 baud / 850 Hz signal looks like an encrypted STANAG 4481 broadcast "
                       "(locked for 10 s, stop bits right only about half the time), print one line in its "
                       "honour, then carry on decoding."));
    connect(egg, &QAction::toggled, m_rtty, &RttyEngine::setEasterEgg);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Check for updates..."), m_update, &UpdateCheck::checkNow);
    help->addAction(tr("About"),
                    this,
                    [this]()
                    {
                        QMessageBox::about(
                            this,
                            tr("About wfview TCI Digi"),
                            tr("<b>wfview TCI Digi %1</b><br><br>"
                               "Digital modes for wfview (or any TCI server) over TCI: receive audio, transmit "
                               "audio, PTT, frequency and mode.<br><br>"
                               "CW: ggmorse (MIT) and a hand-key decoder; transmit as keyed audio.<br>"
                               "RTTY: mark/space filter demodulator with W7AY optimal ATC.<br>"
                               "PSK: BPSK31/63/125, matched-filter receiver with AFC.<br>"
                               "Hell: Feld Hell (Hellschreiber) send and painted receive.<br>"
                               "Olivia / Contestia: Pawel Jalocha's MFSK library, as in fldigi.<br>"
                               "NAVTEX / SITOR-B: receive (decoder after fldigi's).<br>"
                               "SSTV: Martin, Scottie, Robot, PD modes, VIS, auto slant; send pictures.<br>"
                               "WEFAX: HF weather fax receive with APT start/stop and phasing.<br>"
                               "AMTOR: FEC (mode B) send and receive (receiver as NAVTEX).<br>"
                               "WWV Time: time code, clock comparison, audio clock and carrier calibration.<br><br>"
                               "License: GPLv3.<br><br>"
                               "Problems and feature requests: "
                               "<a href=\"https://github.com/KC9oXP/wfview-tci-digi/issues\">"
                               "github.com/KC9oXP/wfview-tci-digi/issues</a>, or look the call sign up on QRZ.<br><br>"
                               "Michael Rippey, KC9oXP")
                                .arg(QStringLiteral(APP_VERSION)));
                    });
}

void MainWindow::applyTheme(bool dark)
{
    qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    if (!dark)
    {
        qApp->setPalette(qApp->style()->standardPalette());
        return;
    }
    QPalette p;
    p.setColor(QPalette::Window, QColor(37, 40, 44));
    p.setColor(QPalette::WindowText, QColor(220, 220, 220));
    p.setColor(QPalette::Base, QColor(24, 26, 29));
    p.setColor(QPalette::AlternateBase, QColor(44, 47, 52));
    p.setColor(QPalette::ToolTipBase, QColor(50, 50, 50));
    p.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
    p.setColor(QPalette::Text, QColor(225, 225, 225));
    p.setColor(QPalette::Button, QColor(50, 54, 59));
    p.setColor(QPalette::ButtonText, QColor(225, 225, 225));
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Highlight, QColor(42, 110, 180));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(90, 160, 230));
    p.setColor(QPalette::PlaceholderText, QColor(130, 130, 130));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
    qApp->setPalette(p);
}

void MainWindow::toggleConnection()
{
    if (m_client->state() != TciClient::State::Disconnected)
    {
        m_wantConnected = false;
        m_reconnect.stop();
        m_client->disconnectFromServer();
        return;
    }
    m_wantConnected = true;
    m_settings->setValue(QStringLiteral("TCI/Host"), m_host->text().trimmed());
    m_settings->setValue(QStringLiteral("TCI/Port"), m_port->value());
    m_client->connectToServer(m_host->text(), quint16(m_port->value()));
}

void MainWindow::onStateChanged(TciClient::State state, const QString &message)
{
    if (!message.isEmpty())
        log(message);
    switch (state)
    {
    case TciClient::State::Disconnected:
        m_connect->setText(tr("Connect"));
        UiStyle::connectButton(m_connect, UiStyle::Link::Off);
        m_connState->setText(message.isEmpty() ? tr("Not connected") : message);
        m_diag->setText(tr("Not connected"));
        m_freq->clear();
        if (m_wantConnected && m_autoReconnect->isChecked())
        {
            m_connState->setText(m_connState->text() + tr(" - retrying in %1 s").arg(reconnectMs / 1000));
            m_reconnect.start();
        }
        break;
    case TciClient::State::Connecting:
        m_connect->setText(tr("Cancel"));
        UiStyle::connectButton(m_connect, UiStyle::Link::Trying);
        m_connState->setText(tr("Connecting..."));
        break;
    case TciClient::State::Connected:
        m_connect->setText(tr("Disconnect"));
        UiStyle::connectButton(m_connect, UiStyle::Link::On);
        m_connState->setText(tr("Connected, waiting for the radio..."));
        break;
    case TciClient::State::Ready:
        m_connect->setText(tr("Disconnect"));
        UiStyle::connectButton(m_connect, UiStyle::Link::On);
        m_connState->setText(tr("%1  (%2)").arg(m_client->device(), m_client->protocol()));
        m_freq->showHz(m_client->frequencyHz());
        break;
    }
    m_connState->setToolTip(m_connState->text());
    m_host->setEnabled(state == TciClient::State::Disconnected);
    m_port->setEnabled(state == TciClient::State::Disconnected);
    updateRadioState();
}

// The ALC reading's scale differs from radio to radio (and by how the server
// reports it), so the warning limit is kept per radio (TCI device name).
QString MainWindow::alcKey() const
{
    const QString dev = AppPaths::safe(m_client->device().trimmed());
    return dev.isEmpty() ? QStringLiteral("Tx/AlcWarn") : QStringLiteral("Tx/AlcWarn-") + dev;
}

void MainWindow::loadAlcLimit()
{
    const double fallback = m_settings->value(QStringLiteral("Tx/AlcWarn"), 1.0).toDouble();
    m_powerWatch->setAlcLimit(m_settings->value(alcKey(), fallback).toDouble());
}

void MainWindow::editAlcLimit()
{
    const QString dev = m_client->device().trimmed();
    bool ok = false;
    const double v = QInputDialog::getDouble(
        this,
        tr("ALC warning"),
        tr("Warn when the ALC reading stays above this value while transmitting (0 = no warning).\n\n"
           "The ALC scale differs between radios, so the limit is kept for each radio%1.\n"
           "Highest ALC in the last transmission: %2\n"
           "Tip: Tune or send at a level you know is clean, look at that number, and set the limit a "
           "little above it.")
            .arg(dev.isEmpty() ? QString() : tr(" (now: %1)").arg(dev))
            .arg(m_powerWatch->lastPeakAlc(), 0, 'f', 1),
        m_powerWatch->alcLimit(),
        0.0,
        100000.0,
        1,
        &ok);
    if (!ok)
        return;
    m_powerWatch->setAlcLimit(v);
    m_settings->setValue(alcKey(), v);
    statusBar()->showMessage(
        v > 0.0 ? tr("ALC warning above %1%2").arg(v, 0, 'f', 1).arg(dev.isEmpty() ? QString() : tr(" for %1").arg(dev))
                : tr("ALC warning off."),
        6000);
}

void MainWindow::onRadioInfo()
{
    loadAlcLimit();
    const QSignalBlocker b(m_mode);
    const QString cur = m_client->modulation();
    m_mode->clear();
    m_mode->addItems(m_client->modulations());
    if (!cur.isEmpty() && m_mode->findText(cur) < 0)
        m_mode->addItem(cur);
    m_mode->setCurrentText(cur);
    if (m_client->state() == TciClient::State::Ready)
        m_connState->setText(tr("%1  (%2)").arg(m_client->device(), m_client->protocol()));
    updateRadioState();
}

void MainWindow::updateWwvRadio()
{
    m_wwv->setRadio(m_client->isReady(), m_client->frequencyHz(), m_client->modulation(), m_client->device());
}

void MainWindow::onFrequency(qint64 hz)
{
    updateWwvRadio();
    m_freq->showHz(hz);
    m_session->setRadio(hz, m_client->modulation());
    m_qso->setRadio(hz, m_client->modulation());
    syncPresetBox();
}

void MainWindow::onModulation(const QString &mode)
{
    const QSignalBlocker b(m_mode);
    if (m_mode->findText(mode) < 0)
        m_mode->addItem(mode);
    m_mode->setCurrentText(mode);
    m_rtty->setRadioMode(mode);
    m_session->setRadio(m_client->frequencyHz(), mode);
    m_qso->setRadio(m_client->frequencyHz(), mode);
    updateRadioState();
}

void MainWindow::onRadioTransmit(bool on)
{
    m_rtty->setRadioTransmitting(on);
    for (ModemEngine *e : m_txEngines)
        e->setRadioTransmitting(on);
    m_cw->setPaused(on); // do not decode our own sidetone / monitor
    m_txLamp->setText(on ? tr(" TX ") : tr(" RX "));
    m_txLamp->setStyleSheet(on ? QStringLiteral("background-color: #c02020; color: white; font-weight: bold;")
                               : QString());
}

void MainWindow::onTransmitRequested(QObject *who, bool on)
{
    if (on)
    {
        // Only one mode may transmit: stop any other first.
        if (who != m_rtty && m_rtty->transmitting())
            m_rtty->abortTx();
        for (ModemEngine *e : m_txEngines)
            if (who != e && e->transmitting())
                e->abortTx();
        if (m_txLimitMin > 0)
            m_txLimit.start(m_txLimitMin * 60 * 1000); // (re)started by each mode keying
    }
    else if (anyTransmitting())
    {
        return; // another mode has taken over; keep PTT
    }
    else
    {
        m_txLimit.stop();
    }
    m_client->setTransmit(on);
    m_powerWatch->setOurTransmit(on);
    if (on)
        m_txHint->hide();
    log(on ? tr("TX requested (trx:0,true)") : tr("TX released (trx:0,false)"));
}

bool MainWindow::anyTransmitting() const
{
    if (m_rtty->transmitting())
        return true;
    for (ModemEngine *e : m_txEngines)
        if (e->transmitting())
            return true;
    return false;
}

void MainWindow::abortAllTx(const QString &why)
{
    Q_UNUSED(why);
    if (m_rtty->transmitting())
        m_rtty->abortTx();
    for (ModemEngine *e : m_txEngines)
        if (e->transmitting())
            e->abortTx();
}

void MainWindow::updateRadioState()
{
    const bool ready = m_client->isReady();
    const bool canTx = ready && !m_client->receiveOnly();
    m_rtty->setConnected(ready, canTx);
    m_rttyPanel->setRadioState(ready, canTx, m_client->modulation());
    for (ModemEngine *e : m_txEngines)
        e->setConnected(ready, canTx);
    m_cwPanel->setRadioState(ready, canTx, m_client->modulation());
    m_pskPanel->setRadioState(ready, canTx, m_client->modulation());
    m_hellPanel->setRadioState(ready, canTx, m_client->modulation());
    m_oliviaPanel->setRadioState(ready, canTx, m_client->modulation());
    m_sstvPanel->setRadioState(ready, canTx, m_client->modulation());
    m_amtorPanel->setRadioState(ready, canTx, m_client->modulation());
    updateWwvRadio();
    m_freq->setEnabled(ready);
    m_mode->setEnabled(ready);
}

void MainWindow::log(const QString &line)
{
    m_log->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz  ")) + line);
}

void MainWindow::saveAll()
{
    m_cwPanel->saveState();
    m_rttyPanel->saveState();
    m_pskPanel->saveState();
    m_hellPanel->saveState();
    m_oliviaPanel->saveState();
    m_sstvPanel->saveState();
    m_amtorPanel->saveState();
    m_settings->beginGroup(QStringLiteral("Window"));
    m_settings->setValue(QStringLiteral("Geometry"), saveGeometry());
    m_settings->setValue(QStringLiteral("State"), saveState());
    m_settings->setValue(QStringLiteral("TabName"), m_tabs->tabText(m_tabs->currentIndex()));
    m_settings->endGroup();
    m_settings->setValue(QStringLiteral("TCI/AutoReconnect"), m_autoReconnect->isChecked());
    m_settings->sync();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    abortAllTx(QString());
    m_wwv->cancelCarrier(); // puts the radio back if a measurement was running
    m_wantConnected = false;
    m_audio->stopRecording();
    m_audio->stopPlayback();
    m_client->disconnectFromServer();
    saveAll();
    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------------------
// Files: screenshots, audio, logs
// ---------------------------------------------------------------------------

void MainWindow::buildFileMenu()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *shot = file->addAction(tr("Save window screenshot"), this, &MainWindow::saveWindowShot);
    shot->setShortcut(QKeySequence(Qt::Key_F12));
    QAction *wf = file->addAction(tr("Save waterfall picture"), this, [this]() { saveWaterfallShot(); });
    wf->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    file->addSeparator();
    QAction *last = file->addAction(
        tr("Save the last 2 minutes of receive audio"),
        this,
        [this]()
        {
            QString err;
            const QString p = m_audio->saveLast(m_audio->ringSeconds(), &err);
            const int sec = qRound(std::min<double>(m_audio->bufferedSeconds(), m_audio->ringSeconds()));
            log(p.isEmpty() ? tr("Audio not saved: %1").arg(err)
                            : tr("Saved %1 s of receive audio to %2").arg(sec).arg(QDir::toNativeSeparators(p)));
            statusBar()->showMessage(p.isEmpty() ? err : QDir::toNativeSeparators(p), 8000);
            // A clear confirmation (the status bar alone was easy to miss).
            auto *box = new QMessageBox(
                p.isEmpty() ? QMessageBox::Warning : QMessageBox::Information,
                tr("Receive audio"),
                p.isEmpty() ? tr("The receive audio was not saved:\n%1").arg(err)
                            : tr("Saved the last %1 s of receive audio:\n%2").arg(sec).arg(QDir::toNativeSeparators(p)),
                QMessageBox::Ok,
                this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setModal(false);
            if (!p.isEmpty())
            {
                QPushButton *open = box->addButton(tr("Open folder"), QMessageBox::ActionRole);
                connect(open,
                        &QPushButton::clicked,
                        this,
                        [p]() { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p).absolutePath())); });
            }
            box->show();
        });
    last->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    connect(file,
            &QMenu::aboutToShow,
            this,
            [this, last]()
            {
                last->setText(tr("Save the last %1 of receive audio")
                                  .arg(m_audio->ringSeconds() >= 120 ? tr("%1 minutes").arg(m_audio->ringSeconds() / 60)
                                                                     : tr("%1 seconds").arg(m_audio->ringSeconds())));
            });
    m_recordAct = file->addAction(tr("Record receive audio"));
    m_recordAct->setCheckable(true);
    connect(m_recordAct,
            &QAction::toggled,
            this,
            [this](bool on)
            {
                if (on == m_audio->recording())
                    return;
                if (on)
                {
                    QString err;
                    if (!m_audio->startRecording(&err))
                    {
                        const QSignalBlocker b(m_recordAct);
                        m_recordAct->setChecked(false);
                        statusBar()->showMessage(err, 8000);
                        return;
                    }
                    log(tr("Recording to %1").arg(QDir::toNativeSeparators(m_audio->recordingPath())));
                }
                else
                {
                    m_audio->stopRecording();
                    log(tr("Recording saved: %1").arg(QDir::toNativeSeparators(m_audio->recordingPath())));
                }
            });
    file->addAction(tr("Play a WAV file into the decoders..."), this, [this]() { playWav(1.0); });
    file->addAction(tr("Play a WAV file, 4x speed..."), this, [this]() { playWav(4.0); });
    m_stopPlayAct = file->addAction(tr("Stop playback"), m_audio, &AudioHub::stopPlayback);
    m_stopPlayAct->setEnabled(false);
    file->addSeparator();
    file->addAction(
        tr("Open data folder"), this, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::base())); });
    file->addAction(tr("Open the logs folder"),
                    this,
                    [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_session->folder())); });
    file->addSeparator();
    file->addAction(tr("Quit"), this, &QWidget::close);
}

void MainWindow::playWav(double speed)
{
    const QString start = m_settings->value(QStringLiteral("Audio/PlayDir"), AppPaths::recordings()).toString();
    const QString path = QFileDialog::getOpenFileName(this, tr("Play a recording"), start, tr("WAV audio (*.wav)"));
    if (path.isEmpty())
        return;
    m_settings->setValue(QStringLiteral("Audio/PlayDir"), QFileInfo(path).absolutePath());
    QString err;
    if (!m_audio->play(path, speed, &err))
    {
        QMessageBox::warning(this, tr("Playback"), tr("Cannot play %1:\n%2").arg(path, err));
        return;
    }
    log(tr("Playing %1 at %2x (radio audio held off until it ends)").arg(QDir::toNativeSeparators(path)).arg(speed));
}

void MainWindow::updateAudioStatus()
{
    QStringList parts;
    if (m_audio->playing())
        parts << tr("<b>Playing</b> %1").arg(QFileInfo(m_audio->playbackFile()).fileName());
    if (m_audio->recording())
        parts << tr("<span style='color:#ff6060'><b>REC</b></span>");
    m_audioState->setText(parts.join(QStringLiteral("  ")));
    if (m_stopPlayAct)
        m_stopPlayAct->setEnabled(m_audio->playing());
    if (m_recordAct && m_recordAct->isChecked() != m_audio->recording())
    {
        const QSignalBlocker b(m_recordAct);
        m_recordAct->setChecked(m_audio->recording());
    }
}

QString MainWindow::stampText() const
{
    const QString call = m_psk->myCall();
    const QString when = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss'Z'"));
    const QString freq
        = m_client->frequencyHz() > 0 ? formatFreq(m_client->frequencyHz()) + QStringLiteral(" Hz") : QString();
    QStringList parts;
    if (!call.isEmpty())
        parts << call;
    parts << when;
    if (!freq.isEmpty())
        parts << freq + QLatin1Char(' ') + m_client->modulation();
    parts << m_tabs->tabText(m_tabs->currentIndex());
    return parts.join(QStringLiteral("   "));
}

void MainWindow::saveWindowShot()
{
    const QPixmap px = grab();
    const QString path
        = QDir(AppPaths::screenshots())
              .filePath(QStringLiteral("window-%1-%2.png")
                            .arg(AppPaths::stamp(), AppPaths::safe(m_tabs->tabText(m_tabs->currentIndex()))));
    if (px.save(path))
    {
        log(tr("Screenshot saved: %1").arg(QDir::toNativeSeparators(path)));
        statusBar()->showMessage(tr("Screenshot saved: %1").arg(QDir::toNativeSeparators(path)), 8000);
    }
}

void MainWindow::saveWaterfallShot(WaterfallWidget *wf)
{
    if (!wf)
        wf = m_tabs->currentWidget() ? m_tabs->currentWidget()->findChild<WaterfallWidget *>() : nullptr;
    if (!wf)
    {
        statusBar()->showMessage(tr("This tab has no waterfall."), 5000);
        return;
    }
    // The waterfall as shown, with a line saying who, when and where.
    const QPixmap px = wf->grab();
    const int bar = fontMetrics().height() + 8;
    QImage out(px.width(), px.height() + bar, QImage::Format_RGB32);
    out.fill(QColor(20, 22, 26));
    QPainter p(&out);
    p.drawPixmap(0, 0, px);
    p.setPen(QColor(230, 230, 230));
    p.drawText(QRect(6, px.height(), out.width() - 12, bar), Qt::AlignVCenter | Qt::AlignLeft, stampText());
    p.end();
    const QString path
        = QDir(AppPaths::screenshots())
              .filePath(QStringLiteral("waterfall-%1-%2.png")
                            .arg(AppPaths::stamp(), AppPaths::safe(m_tabs->tabText(m_tabs->currentIndex()))));
    if (out.save(path))
    {
        log(tr("Waterfall saved: %1").arg(QDir::toNativeSeparators(path)));
        statusBar()->showMessage(tr("Waterfall saved: %1").arg(QDir::toNativeSeparators(path)), 8000);
    }
}

void MainWindow::wireLogs()
{
    // Session log: what each mode decodes (and sends), labelled with the mode.
    connect(m_cw,
            &CwDecoder::textAppended,
            this,
            [this](const QString &t) { m_session->add(QStringLiteral("CW"), tr("CW"), t); });
    connect(m_cwTx,
            &ModemEngine::textAppended,
            this,
            [this](const QString &t) { m_session->add(QStringLiteral("CW"), tr("CW"), t); });
    connect(m_rtty,
            &RttyEngine::textAppended,
            this,
            [this](const QString &t)
            {
                m_session->add(QStringLiteral("RTTY"),
                               tr("RTTY %1 Bd / %2 Hz").arg(m_rtty->baud(), 0, 'g', 4).arg(m_rtty->shift()),
                               t);
            });
    connect(m_psk,
            &ModemEngine::textAppended,
            this,
            [this](const QString &t)
            {
                m_session->add(QStringLiteral("PSK"),
                               tr("PSK %1 @ %2 Hz")
                                   .arg(QString::fromLatin1(psk::speedName(m_psk->speed())))
                                   .arg(qRound(m_psk->carrierHz())),
                               t);
            });
    connect(m_hell,
            &ModemEngine::textAppended,
            this,
            [this](const QString &t) { m_session->add(QStringLiteral("Hell"), tr("Feld Hell (sent)"), t); });
    connect(m_olivia,
            &ModemEngine::textAppended,
            this,
            [this](const QString &t)
            {
                m_session->add(QStringLiteral("Olivia"),
                               tr("%1 @ %2 Hz").arg(m_olivia->modeName()).arg(qRound(m_olivia->carrierHz())),
                               t);
            });
    connect(m_amtor,
            &ModemEngine::textAppended,
            this,
            [this](const QString &t)
            { m_session->add(QStringLiteral("AMTOR"), tr("AMTOR FEC @ %1 Hz").arg(qRound(m_amtor->carrierHz())), t); });
    connect(m_navtex,
            &NavtexEngine::textAppended,
            this,
            [this](const QString &t)
            { m_session->add(QStringLiteral("NAVTEX"), tr("NAVTEX @ %1 Hz").arg(qRound(m_navtex->centreHz())), t); });
    connect(m_sstv,
            &SstvEngine::logLine,
            this,
            [this](const QString &t) { m_session->add(QStringLiteral("SSTV"), tr("SSTV"), t); });
    connect(m_wefax,
            &WefaxEngine::logLine,
            this,
            [this](const QString &t) { m_session->add(QStringLiteral("WEFAX"), tr("WEFAX"), t); });

    // QSO log: the other station's call follows whichever mode set it last.
    const auto followCall = [this](const QString &c) { m_qso->setCall(c); };
    connect(m_rtty, &RttyEngine::settingsChanged, this, [this, followCall]() { followCall(m_rtty->call()); });
    for (ModemEngine *e : m_modems)
        connect(e,
                &ModemEngine::settingsChanged,
                this,
                [this, e, followCall]()
                {
                    followCall(e->call());
                    m_qso->setMyCall(e->myCall());
                });
    m_qso->setMyCall(m_psk->myCall());
    connect(m_qso,
            &QsoLogDock::logged,
            this,
            [this](const QString &line)
            {
                log(line);
                statusBar()->showMessage(line, 6000);
            });
    connect(m_psk, &ModemEngine::settingsChanged, this, &MainWindow::updateQsoMode);
    connect(m_olivia, &ModemEngine::settingsChanged, this, &MainWindow::updateQsoMode);

    // A callsign double-clicked (or "Log <call>") in any receive pane goes to the QSO log.
    connect(RxCalls::instance(),
            &RxCalls::callPicked,
            this,
            [this](const QString &call, const QString &)
            {
                m_qso->setCall(call);
                m_qso->show();
                m_qso->raise();
                // You picked it from what you received: you heard it, so report it.
                QString note;
                const QString mode = PskReporter::modeFor(currentTabKey(), currentSubMode());
                if (m_pskr->enabled() && !mode.isEmpty())
                {
                    const int before = m_pskr->waiting();
                    m_pskr->addSpot(call, currentSignalHz(), mode);
                    note = m_pskr->waiting() > before ? tr(" Queued for PSK Reporter.")
                                                      : tr(" (Already reported to PSK Reporter recently.)");
                }
                statusBar()->showMessage(
                    tr("%1 in the QSO log: Log QSO (Ctrl+L) writes it to the ADIF file.%2").arg(call, note), 8000);
            });
    wirePskReporter();
    // Each logged QSO: to another program, QRZ and/or LoTW as set in Logging and uploads.
    connect(m_qso, &QsoLogDock::qsoLogged, m_uploader, &LogUploader::qsoLogged);
    updateUploadButtons();
}

void MainWindow::updateQsoMode()
{
    if (!m_qso)
        return;
    QWidget *w = m_tabs->currentWidget();
    QString mode, sub;
    if (w == m_cwPanel)
        mode = QStringLiteral("CW");
    else if (w == m_rttyPanel)
        mode = QStringLiteral("RTTY");
    else if (w == m_pskPanel)
    {
        mode = QStringLiteral("PSK");
        sub = QStringList {QStringLiteral("PSK31"), QStringLiteral("PSK63"), QStringLiteral("PSK125")}.value(
            m_psk->speed());
    }
    else if (w == m_hellPanel)
    {
        mode = QStringLiteral("HELL");
        sub = QStringLiteral("FMHELL");
    }
    else if (w == m_oliviaPanel)
    {
        if (m_olivia->contestia())
            mode = QStringLiteral("CONTESTI");
        else
        {
            mode = QStringLiteral("OLIVIA");
            sub = QStringLiteral("OLIVIA %1/%2").arg(m_olivia->tones()).arg(m_olivia->bandwidth());
        }
    }
    else if (w == m_sstvPanel)
        mode = QStringLiteral("SSTV");
    else if (w == m_amtorPanel)
    {
        mode = QStringLiteral("TOR");
        sub = QStringLiteral("AMTORFEC");
    }
    // NAVTEX and WEFAX are receive-only: no QSO mode.
    m_qso->setMode(mode, sub, m_tabs->tabText(m_tabs->currentIndex()));
}

// ---------------------------------------------------------------------------
// Frequency presets (per tab)
// ---------------------------------------------------------------------------

QString MainWindow::currentTabKey() const
{
    return m_tabs ? m_tabs->tabText(m_tabs->currentIndex()) : QString();
}

QList<MainWindow::Preset> MainWindow::presets(const QString &tabKey) const
{
    QList<Preset> out;
    const QString key = QStringLiteral("Presets/") + AppPaths::safe(tabKey);
    QStringList lines;
    if (m_settings->contains(key))
        lines = m_settings->value(key).toStringList();
    else
    {
        // Defaults: dial frequency (Hz) and sideband.
        static const QMap<QString, QStringList> defaults = {
            {QStringLiteral("CW"),
             {QStringLiteral("80 m|3550000|USB"),
              QStringLiteral("40 m|7030000|USB"),
              QStringLiteral("30 m|10120000|USB"),
              QStringLiteral("20 m|14050000|USB")}},
            {QStringLiteral("RTTY"),
             {QStringLiteral("80 m|3580000|USB"),
              QStringLiteral("40 m|7080000|USB"),
              QStringLiteral("20 m|14080000|USB"),
              QStringLiteral("15 m|21080000|USB")}},
            {QStringLiteral("PSK"),
             {QStringLiteral("80 m|3580000|USB"),
              QStringLiteral("40 m|7070000|USB"),
              QStringLiteral("30 m|10142000|USB"),
              QStringLiteral("20 m|14070000|USB"),
              QStringLiteral("15 m|21070000|USB"),
              QStringLiteral("10 m|28120000|USB")}},
            {QStringLiteral("Olivia"),
             {QStringLiteral("80 m|3583000|USB"),
              QStringLiteral("40 m|7072000|USB"),
              QStringLiteral("20 m|14072000|USB")}},
            {QStringLiteral("SSTV"),
             {QStringLiteral("80 m 3.845 LSB|3845000|LSB"),
              QStringLiteral("40 m 7.171 LSB|7171000|LSB"),
              QStringLiteral("20 m 14.230|14230000|USB"),
              QStringLiteral("20 m 14.233|14233000|USB"),
              QStringLiteral("15 m 21.340|21340000|USB"),
              QStringLiteral("10 m 28.680|28680000|USB")}},
            {QStringLiteral("NAVTEX"),
             {QStringLiteral("518 kHz (dial 517.0)|517000|USB"),
              QStringLiteral("490 kHz (dial 489.0)|489000|USB"),
              QStringLiteral("4209.5 kHz (dial 4208.5)|4208500|USB")}},
            {QStringLiteral("WEFAX"),
             {QStringLiteral("NMF Boston 4235|4233100|USB"),
              QStringLiteral("NMF Boston 6340.5|6338600|USB"),
              QStringLiteral("NMF Boston 9110|9108100|USB"),
              QStringLiteral("NMF Boston 12750|12748100|USB"),
              QStringLiteral("NMG New Orleans 4317.9|4316000|USB"),
              QStringLiteral("NMG New Orleans 8503.9|8502000|USB"),
              QStringLiteral("NMG New Orleans 12789.9|12788000|USB")}},
            {QStringLiteral("WWV Time"),
             {QStringLiteral("2.5 MHz|2500000|AM"),
              QStringLiteral("5 MHz|5000000|AM"),
              QStringLiteral("10 MHz|10000000|AM"),
              QStringLiteral("15 MHz|15000000|AM"),
              QStringLiteral("20 MHz|20000000|AM"),
              QStringLiteral("25 MHz (WWV)|25000000|AM")}},
        };
        lines = defaults.value(tabKey);
    }
    for (const QString &l : lines)
    {
        const QStringList f = l.split(QLatin1Char('|'));
        Preset p;
        p.name = f.value(0).trimmed();
        p.hz = f.value(1).trimmed().toLongLong();
        p.sideband = f.value(2).trimmed().toUpper();
        if (p.sideband != QLatin1String("USB") && p.sideband != QLatin1String("LSB")
            && p.sideband != QLatin1String("AM"))
            p.sideband.clear();
        if (!p.name.isEmpty() && p.hz > 0)
            out << p;
    }
    return out;
}

void MainWindow::savePresets(const QString &tabKey, const QList<Preset> &list)
{
    QStringList lines;
    for (const Preset &p : list)
        lines << QStringLiteral("%1|%2|%3").arg(QString(p.name).remove(QLatin1Char('|'))).arg(p.hz).arg(p.sideband);
    m_settings->setValue(QStringLiteral("Presets/") + AppPaths::safe(tabKey), lines);
}

void MainWindow::fillPresetBox()
{
    if (!m_presetBox || !m_tabs)
        return;
    const QSignalBlocker b(m_presetBox);
    m_presetBox->clear();
    const QString key = currentTabKey();
    const QList<Preset> list = presets(key);
    m_presetBox->addItem(tr("Presets"), QString());
    for (int i = 0; i < list.size(); ++i)
    {
        const Preset &p = list.at(i);
        m_presetBox->addItem(QStringLiteral("%1   %2 %3").arg(p.name, formatFreq(p.hz), p.sideband),
                             QStringLiteral("preset"));
        m_presetBox->setItemData(m_presetBox->count() - 1, i, Qt::UserRole + 1);
    }
    m_presetBox->insertSeparator(m_presetBox->count());
    m_presetBox->addItem(tr("Add the current frequency..."), QStringLiteral("add"));
    m_presetBox->addItem(tr("Edit presets..."), QStringLiteral("edit"));
    m_presetBox->setCurrentIndex(0);
    m_presetBox->setToolTip(tr("Frequency presets for the %1 tab: set the dial (and USB/LSB) over TCI.").arg(key));
    syncPresetBox();
}

QStringList MainWindow::tabOrder() const
{
    QStringList keys;
    for (int i = 0; i < m_tabs->count(); ++i)
        keys << m_tabs->widget(i)->property("tabKey").toString();
    return keys;
}

void MainWindow::arrangeTabs(const QStringList &order)
{
    // Wanted order: the saved keys first (unknown ones skipped), then any tab not in it in its
    // built-in place; then transmit tabs, "Read only", receive-only tabs.
    QList<QWidget *> all;
    for (int i = 0; i < m_tabs->count(); ++i)
        all << m_tabs->widget(i);
    QList<QWidget *> wanted;
    for (const QString &k : order)
        for (QWidget *w : all)
            if (w->property("tabKey").toString() == k && !wanted.contains(w))
                wanted << w;
    if (order.isEmpty())
    {
        // built-in order
        wanted = {m_cwPanel,
                  m_rttyPanel,
                  m_pskPanel,
                  m_hellPanel,
                  m_oliviaPanel,
                  m_sstvPanel,
                  m_amtorPanel,
                  m_tabDivider,
                  m_navtexPanel,
                  m_wefaxPanel,
                  m_wwvPanel};
    }
    for (QWidget *w : all)
        if (!wanted.contains(w))
            wanted << w;
    QList<QWidget *> tx, rx;
    for (QWidget *w : wanted)
    {
        if (w == m_tabDivider)
            continue;
        (w->property("tabTx").toBool() ? tx : rx) << w;
    }
    const QList<QWidget *> target = tx + QList<QWidget *>{m_tabDivider} + rx;

    QWidget *current = m_tabs->currentWidget();
    m_arrangingTabs = true;
    for (int t = 0; t < target.size(); ++t)
    {
        const int from = m_tabs->indexOf(target.at(t));
        if (from >= 0 && from != t)
            m_tabs->tabBar()->moveTab(from, t);
    }
    m_arrangingTabs = false;
    if (current)
        m_tabs->setCurrentWidget(current);
}

void MainWindow::tabsMoved()
{
    arrangeTabs(tabOrder());
    m_settings->setValue(QStringLiteral("Window/TabOrder"), tabOrder());
}

void MainWindow::syncPresetBox()
{
    if (!m_presetBox || !m_client)
        return;
    const qint64 hz = m_client->frequencyHz();
    const QString mode = m_client->modulation().toUpper();
    int match = 0;
    for (int i = 1; i < m_presetBox->count(); ++i)
    {
        if (m_presetBox->itemData(i).toString() != QLatin1String("preset"))
            continue;
        const QList<Preset> list = presets(currentTabKey());
        const int k = m_presetBox->itemData(i, Qt::UserRole + 1).toInt();
        if (k < 0 || k >= list.size())
            continue;
        const Preset &p = list.at(k);
        // same dial (10 Hz) and, if the preset sets one, the same sideband (USB also matches DIGU)
        const bool sbOk = p.sideband.isEmpty()
                          || (p.sideband == QLatin1String("USB") ? !mode.startsWith(QLatin1String("L"))
                                                                  : mode.startsWith(QLatin1String("L")));
        if (hz > 0 && std::llabs(hz - p.hz) <= 10 && sbOk)
        {
            match = i;
            break;
        }
    }
    if (m_presetBox->currentIndex() != match)
    {
        const QSignalBlocker b(m_presetBox);
        m_presetBox->setCurrentIndex(match);
    }
}

void MainWindow::applyPreset(const Preset &p, const QString &what)
{
    if (!m_client->isReady())
    {
        statusBar()->showMessage(tr("Connect to wfview first."), 5000);
        return;
    }
    m_client->setFrequency(p.hz);
    QString note;
    const QString cur = m_client->modulation();
    if (p.sideband == QLatin1String("AM"))
    {
        if (RadioModes::normalised(cur) != QLatin1String("AM"))
        {
            QString am = QStringLiteral("am");
            for (const QString &m : m_client->modulations())
                if (RadioModes::normalised(m) == QLatin1String("AM"))
                    am = m;
            m_client->setModulation(am);
            note = tr(", mode AM");
        }
    }
    else if (!p.sideband.isEmpty() && RadioModes::isLsb(cur) != (p.sideband == QLatin1String("LSB")))
    {
        // Change the sideband but stay a data mode if the radio is in one. The mode
        // is only sent when the sideband differs: wfview puts an Icom on Data 1
        // whenever DIGU/DIGL is set over TCI.
        const bool wantLsb = p.sideband == QLatin1String("LSB");
        QString pick;
        const QStringList mods = m_client->modulations();
        if (RadioModes::isData(cur))
            for (const QString &m : mods)
                if (RadioModes::isData(m) && RadioModes::isLsb(m) == wantLsb)
                {
                    pick = m;
                    break;
                }
        if (pick.isEmpty())
            for (const QString &m : mods)
                if (RadioModes::kind(m) == RadioModes::Kind::Voice && RadioModes::isLsb(m) == wantLsb)
                {
                    pick = m;
                    break;
                }
        if (pick.isEmpty())
            pick = wantLsb ? QStringLiteral("lsb") : QStringLiteral("usb");
        m_client->setModulation(pick);
        note = tr(", mode %1").arg(pick.toUpper());
        if (RadioModes::isData(pick))
            note += tr(" (check wfview's data mode: Data 1 is set this way)");
    }
    const QString t = tr("%1 %2: %3 Hz%4").arg(what.isEmpty() ? tr("Preset") : what, p.name, formatFreq(p.hz), note);
    log(t);
    statusBar()->showMessage(t, 6000);
}

void MainWindow::addCurrentPreset()
{
    const qint64 hz = m_client->frequencyHz();
    if (hz <= 0)
    {
        statusBar()->showMessage(tr("No frequency yet: connect to wfview first."), 5000);
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this,
                                               tr("Add preset"),
                                               tr("Name for %1 Hz on the %2 tab:").arg(formatFreq(hz), currentTabKey()),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    QList<Preset> list = presets(currentTabKey());
    Preset p;
    p.name = name;
    p.hz = hz;
    const QString mode = m_client->modulation();
    p.sideband = RadioModes::carriesAudio(mode)
        ? (RadioModes::isLsb(mode) ? QStringLiteral("LSB") : QStringLiteral("USB"))
        : QString();
    list << p;
    savePresets(currentTabKey(), list);
    fillPresetBox();
}

void MainWindow::editPresets()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("%1 presets").arg(currentTabKey()));
    auto *v = new QVBoxLayout(&dlg);
    auto *help = new QLabel(tr("One per line:  name | dial frequency in Hz | USB, LSB, AM or empty (keep the mode).\n"
                               "Delete everything to get the built-in list back."));
    help->setWordWrap(true);
    v->addWidget(help);
    auto *edit = new QPlainTextEdit();
    QStringList lines;
    for (const Preset &p : presets(currentTabKey()))
        lines << QStringLiteral("%1 | %2 | %3").arg(p.name).arg(p.hz).arg(p.sideband);
    edit->setPlainText(lines.join(QLatin1Char('\n')));
    v->addWidget(edit, 1);
    auto *row = new QHBoxLayout();
    auto *okBtn = new QPushButton(tr("OK"));
    auto *cancel = new QPushButton(tr("Cancel"));
    row->addStretch(1);
    row->addWidget(okBtn);
    row->addWidget(cancel);
    v->addLayout(row);
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlg.resize(560, 360);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString text = edit->toPlainText().trimmed();
    if (text.isEmpty())
    {
        m_settings->remove(QStringLiteral("Presets/") + AppPaths::safe(currentTabKey()));
        fillPresetBox();
        return;
    }
    QList<Preset> list;
    for (const QString &l : text.split(QLatin1Char('\n')))
    {
        const QStringList f = l.split(QLatin1Char('|'));
        Preset p;
        p.name = f.value(0).trimmed();
        p.hz = parseFreq(f.value(1));
        p.sideband = f.value(2).trimmed().toUpper();
        if (p.sideband != QLatin1String("USB") && p.sideband != QLatin1String("LSB")
            && p.sideband != QLatin1String("AM"))
            p.sideband.clear();
        if (!p.name.isEmpty() && p.hz > 0)
            list << p;
    }
    savePresets(currentTabKey(), list);
    fillPresetBox();
}

// ---------------------------------------------------------------------------
// Software memories
// ---------------------------------------------------------------------------

void MainWindow::showMemories()
{
    if (!m_memories)
    {
        QStringList tabs;
        for (int i = 0; i < m_tabs->count(); ++i)
            if (m_tabs->isTabEnabled(i))
                tabs << m_tabs->tabText(i);
        m_memories = new MemoriesDialog(m_settings, tabs, this);
        m_memories->setCurrentProvider([this]() { return currentMemory(); });
        connect(m_memories, &MemoriesDialog::tuneRequested, this, &MainWindow::applyMemory);
    }
    m_memories->setCurrentTab(currentTabKey());
    m_memories->show();
    m_memories->raise();
    m_memories->activateWindow();
}

memories::Memory MainWindow::currentMemory() const
{
    memories::Memory m;
    m.tab = currentTabKey().toStdString();
    m.dialHz = m_client->frequencyHz();
    const QString mode = m_client->modulation();
    if (RadioModes::normalised(mode) == QLatin1String("AM"))
        m.sideband = "AM";
    else if (RadioModes::carriesAudio(mode))
        m.sideband = RadioModes::isLsb(mode) ? "LSB" : "USB";
    QWidget *w = m_tabs->currentWidget();
    if (w == m_pskPanel)
        m.subMode = psk::speedName(m_psk->speed());
    else if (w == m_oliviaPanel)
        m.subMode
            = OliviaEngine::presetName({m_olivia->contestia(), m_olivia->tones(), m_olivia->bandwidth()}).toStdString();
    else if (w == m_rttyPanel)
        m.subMode = QStringLiteral("%1/%2").arg(m_rtty->baud(), 0, 'g', 4).arg(m_rtty->shift()).toStdString();
    return m;
}

// Sets the sub-mode of a tab from its text ("BPSK31", "Olivia 8/500", "45.45/170").
// Returns a note for the status bar when the text was not understood.
QString MainWindow::applySubMode(const QString &tab, const QString &subIn)
{
    const QString sub = subIn.trimmed();
    if (sub.isEmpty())
        return QString();
    QString norm = sub.toUpper();
    norm.remove(QLatin1Char(' '));
    if (tab.compare(QLatin1String("PSK"), Qt::CaseInsensitive) == 0)
    {
        for (int sp = psk::Bpsk31; sp <= psk::Bpsk125; ++sp)
        {
            const QString n = QString::fromLatin1(psk::speedName(sp));
            if (norm == n || QStringLiteral("B") + norm == n) // "PSK31" works too
            {
                m_psk->setSpeed(sp);
                return QString();
            }
        }
    }
    else if (tab.compare(QLatin1String("Olivia"), Qt::CaseInsensitive) == 0)
    {
        for (const OliviaEngine::Preset &p : OliviaEngine::presets())
        {
            QString n = OliviaEngine::presetName(p).toUpper();
            n.remove(QLatin1Char(' '));
            if (norm == n)
            {
                m_olivia->setMode(p.contestia, p.tones, p.bandwidth);
                return QString();
            }
        }
    }
    else if (tab.compare(QLatin1String("RTTY"), Qt::CaseInsensitive) == 0)
    {
        // "45.45/170" (baud / shift)
        const QStringList f = sub.split(QLatin1Char('/'));
        bool okB = false, okS = false;
        const double baud = f.value(0).trimmed().toDouble(&okB);
        const int shift = f.size() > 1 ? f.value(1).trimmed().toInt(&okS) : 0;
        if (okB && baud >= 20 && baud <= 300)
        {
            m_rtty->setBaud(baud);
            if (okS && shift >= 20 && shift <= 1000)
                m_rtty->setShift(shift);
            return QString();
        }
    }
    else
        return tr("(sub-mode \"%1\" is not used on the %2 tab)").arg(sub, tab);
    return tr("(sub-mode \"%1\" not known: left as it was)").arg(sub);
}

void MainWindow::applyMemory(const memories::Memory &m)
{
    if (anyTransmitting())
    {
        statusBar()->showMessage(tr("Not while transmitting."), 5000);
        return;
    }
    const QString tab = QString::fromStdString(m.tab);
    // Show the memory's tab (if there is one by that name).
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->isTabEnabled(i) && m_tabs->tabText(i).compare(tab, Qt::CaseInsensitive) == 0)
        {
            m_tabs->setCurrentIndex(i);
            break;
        }
    const QString subNote = applySubMode(currentTabKey(), QString::fromStdString(m.subMode));
    if (!subNote.isEmpty())
        log(QStringLiteral("Memory %1 %2").arg(QString::fromStdString(m.name), subNote));
    Preset p;
    p.name = QString::fromStdString(m.name);
    p.hz = m.dialHz;
    p.sideband = QString::fromStdString(m.sideband);
    applyPreset(p, tr("Memory"));
}

// ---------------------------------------------------------------------------
// PSK Reporter
// ---------------------------------------------------------------------------

qint64 MainWindow::rfHz(double audioHz) const
{
    const qint64 dial = m_client->frequencyHz();
    if (dial <= 0)
        return 0;
    const qint64 a = qRound64(audioHz);
    return RadioModes::isLsb(m_client->modulation()) ? dial - a : dial + a;
}

void MainWindow::feedPskr(const QString &tab, const QString &subMode, const QString &text, double audioHz)
{
    const bool pskr = m_pskr->enabled() && m_pskr->autoDetect();
    const bool gt = m_gridTracker->enabled();
    if (!pskr && !gt)
        return;
    // Only what was received: our own sent text (and anything while keyed) is skipped.
    if (anyTransmitting() || m_client->radioTransmitting() || text.contains(QLatin1String("[TX]")))
        return;
    const QString mode = PskReporter::modeFor(tab, subMode);
    const qint64 rf = rfHz(audioHz);
    if (pskr)
        m_pskr->feedText(tab, mode, text, rf);
    if (gt)
    {
        // The live SNR only if it was measured recently on this signal.
        const bool snrOk = !std::isnan(m_snr) && m_snrAge.isValid() && m_snrAge.elapsed() < 10000
                           && std::abs(m_snrHz - audioHz) < 200;
        m_gridTracker->feedText(tab, mode, text, audioHz, rf, snrOk ? m_snr : std::nan(""));
    }
}

void MainWindow::updateGridTracker()
{
    if (!m_gridTracker->enabled())
        return;
    m_gridTracker->setDial(m_client->frequencyHz());
    const QString tab = currentTabKey();
    QString mode = PskReporter::modeFor(tab, currentSubMode());
    if (mode.isEmpty())
        mode = QString(tab).remove(QLatin1Char('&')).toUpper(); // SSTV, HELL ... (no PSK Reporter name)
    m_gridTracker->setMode(mode);
    m_gridTracker->setTransmitting(anyTransmitting() || m_client->radioTransmitting());
    m_gridTracker->setRxDf(currentAudioHz());
}

void MainWindow::wirePskReporter()
{
    connect(m_cw,
            &CwDecoder::textAppended,
            this,
            [this](const QString &t) { feedPskr(QStringLiteral("CW"), QString(), t, m_cw->currentPitchHz()); });
    connect(m_rtty,
            &RttyEngine::textAppended,
            this,
            [this](const QString &t)
            { feedPskr(QStringLiteral("RTTY"), QString(), t, m_rtty->markHz() - m_rtty->shift() / 2.0); });
    connect(m_psk,
            &ModemEngine::textAppended,
            this,
            [this](const QString &t) {
                feedPskr(
                    QStringLiteral("PSK"), QString::fromLatin1(psk::speedName(m_psk->speed())), t, m_psk->carrierHz());
            });
    connect(m_olivia,
            &ModemEngine::textAppended,
            this,
            [this](const QString &t)
            { feedPskr(QStringLiteral("Olivia"), m_olivia->modeName(), t, m_olivia->carrierHz()); });
    connect(m_amtor,
            &ModemEngine::textAppended,
            this,
            [this](const QString &t) { feedPskr(QStringLiteral("AMTOR"), QString(), t, m_amtor->carrierHz()); });

    // Every logged QSO is reported (when PSK Reporter is on).
    connect(m_qso,
            &QsoLogDock::qsoLogged,
            this,
            [this](const Qso &q)
            {
                QString mode = q.mode;
                if (q.mode == QLatin1String("PSK") && !q.submode.isEmpty())
                    mode = q.submode; // PSK31 / PSK63 / PSK125
                else if (q.mode == QLatin1String("TOR"))
                    mode = QStringLiteral("AMTORFEC");
                // The RF frequency of the signal on the tab in use, else the dial.
                const qint64 hz = currentSignalHz();
                m_pskr->addSpot(q.call, hz > 0 ? hz : q.freqHz, mode);
            });

    // Station: the call from the tabs' My call boxes, the grid from the QSO log.
    m_pskr->setMyCall(m_psk->myCall().isEmpty() ? m_rtty->myCall() : m_psk->myCall());
    connect(m_rtty, &RttyEngine::settingsChanged, this, [this]() { m_pskr->setMyCall(m_rtty->myCall()); });
    for (ModemEngine *e : m_modems)
        connect(e, &ModemEngine::settingsChanged, this, [this, e]() { m_pskr->setMyCall(e->myCall()); });
    connect(m_qso, &QsoLogDock::myGridChanged, m_pskr, &PskReporter::setMyGrid);

    // GridTracker gets the same call and grid.
    m_gridTracker->setMyCall(m_pskr->myCall());
    m_gridTracker->setMyGrid(m_pskr->myGrid());
    connect(m_pskr, &PskReporter::statusChanged, this, [this]() {
        m_gridTracker->setMyCall(m_pskr->myCall());
        m_gridTracker->setMyGrid(m_pskr->myGrid());
    });

    connect(m_pskr, &PskReporter::statusChanged, this, &MainWindow::updatePskrButton);
    connect(m_pskr, &PskReporter::message, this, &MainWindow::log);
    m_pskrRefresh.setInterval(15000); // the tooltip shows when the next send is
    connect(&m_pskrRefresh, &QTimer::timeout, this, &MainWindow::updatePskrButton);
    m_pskrRefresh.start();
    updatePskrButton();
}

void MainWindow::updatePskrButton()
{
    if (!m_pskrBtn)
        return;
    const bool on = m_pskr->enabled();
    const bool problem = on && (!m_pskr->configured() || !m_pskr->lastError().isEmpty());
    QString text = on ? tr("PSKR %1").arg(m_pskr->spotsSent()) : tr("PSKR off");
    if (on && m_pskr->testServer())
        text += tr(" (test)");
    m_pskrBtn->setText(text);
    // green on, amber on with a problem, grey off
    const QString bg = !on ? QStringLiteral("transparent")
        : problem          ? QStringLiteral("#d08a00")
                           : QStringLiteral("#2e8b3a");
    const QString fg = on ? QStringLiteral("white") : palette().color(QPalette::Disabled, QPalette::WindowText).name();
    m_pskrBtn->setStyleSheet(
        QStringLiteral("QPushButton { background: %1; color: %2; border-radius: 3px; padding: 1px 6px; "
                       "font-weight: %3; }")
            .arg(bg, fg, on ? QStringLiteral("bold") : QStringLiteral("normal")));
    m_pskrBtn->setToolTip(m_pskr->statusText() + tr("\nRight-click or Options > PSK Reporter... for settings."));
}

void MainWindow::togglePskReporter()
{
    if (m_pskr->enabled())
    {
        m_pskr->setEnabled(false);
        return;
    }
    if (!m_pskr->configured())
    {
        editPskReporter(); // asks for the call and grid, then turns it on
        return;
    }
    m_pskr->setEnabled(true);
}

void MainWindow::editPskReporter()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("PSK Reporter"));
    auto *form = new QFormLayout(&dlg);
    auto *intro = new QLabel(tr("Sends the callsigns this station hears to pskreporter.info, where they show on "
                                "the map. Reports go out at most every 5 minutes."));
    intro->setWordWrap(true);
    form->addRow(intro);
    auto *enable = new QCheckBox(tr("Report to PSK Reporter"));
    enable->setChecked(m_pskr->enabled() || !m_pskr->configured());
    form->addRow(enable);
    auto *autoDet = new QCheckBox(tr("Auto-detect callsigns in the decoded text"));
    autoDet->setChecked(m_pskr->autoDetect());
    autoDet->setToolTip(tr("Off: only QSOs you log are reported.\nOn: also any call seen once right after CQ, DE or 73 "
                           "(\"CQ DE W1AW K\", \"73 W1AW SK\"), in the CW, RTTY, PSK, Olivia/Contestia and AMTOR "
                           "tabs; each call at most once per 10 minutes."));
    form->addRow(autoDet);
    auto *callEdit = new QLineEdit(m_pskr->myCall());
    callEdit->setPlaceholderText(tr("e.g. KC9OXP"));
    callEdit->setToolTip(tr("Your call sign. It is the same My call that every transmit tab and the QSO log use."));
    callEdit->setMaxLength(16);
    form->addRow(tr("Your call"), callEdit);
    auto *grid = new QLineEdit(m_pskr->myGrid());
    grid->setPlaceholderText(tr("e.g. EN71ar"));
    grid->setToolTip(tr("Your Maidenhead grid, 4 or 6 characters (also the QSO log's My grid)."));
    form->addRow(tr("Your grid"), grid);
    auto *ant = new QLineEdit(m_pskr->antenna());
    ant->setPlaceholderText(tr("optional, e.g. 314 ft random wire"));
    form->addRow(tr("Antenna"), ant);
    auto *test = new QCheckBox(tr("Send to the PSK Reporter test server (nothing shows on the map)"));
    test->setChecked(m_pskr->testServer());
    test->setToolTip(tr("pskreporter.info port 14739: the packets are checked, see "
                        "https://pskreporter.info/cgi-bin/psk-analysis.pl"));
    form->addRow(test);
    auto *status = new QLabel(m_pskr->statusText());
    status->setWordWrap(true);
    form->addRow(status);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb,
            &QDialogButtonBox::accepted,
            &dlg,
            [&]()
            {
                const QString c = callEdit->text().trimmed().toUpper();
                if (!c.isEmpty() && !callspot::isCallsign(c.toStdString()))
                {
                    QMessageBox::information(&dlg, dlg.windowTitle(), tr("%1 does not look like a call sign.").arg(c));
                    return;
                }
                if (enable->isChecked() && c.isEmpty())
                {
                    QMessageBox::information(&dlg, dlg.windowTitle(), tr("Type your call sign to turn reporting on."));
                    return;
                }
                if (enable->isChecked() && !PskReporter::validGrid(grid->text()))
                {
                    QMessageBox::information(&dlg, dlg.windowTitle(), tr("Your grid should look like EN71 or EN71ar."));
                    return;
                }
                dlg.accept();
            });
    if (dlg.exec() != QDialog::Accepted)
        return;
    // One My call for the whole app: setting it on one engine shares it with every tab, the QSO log and PSKR.
    const QString newCall = callEdit->text().trimmed().toUpper();
    if (!newCall.isEmpty() && newCall != m_psk->myCall())
        m_psk->setMyCall(newCall);
    m_pskr->setMyCall(newCall.isEmpty() ? m_pskr->myCall() : newCall);
    m_pskr->setMyGrid(grid->text());
    m_qso->setMyGrid(grid->text());
    m_pskr->setAntenna(ant->text());
    m_pskr->setTestServer(test->isChecked());
    m_pskr->setAutoDetect(autoDet->isChecked());
    m_pskr->setEnabled(enable->isChecked());
    updatePskrButton();
}

// ---------------------------------------------------------------------------
// Upload indicators (status bar): shown only for the services that are on
// ---------------------------------------------------------------------------

void MainWindow::updateUploadButtons()
{
    if (!m_uploader || !m_qrzBtn)
        return;
    const auto paint = [this](QPushButton *b, bool on, LogUploader::State st, int waiting, const QString &tip)
    {
        b->setVisible(on);
        if (!on)
            return;
        QString bg = QStringLiteral("#2e8b3a"); // ok: green
        if (st == LogUploader::State::Busy)
            bg = QStringLiteral("#3a6ea5"); // sending: blue
        else if (st == LogUploader::State::Waiting)
            bg = QStringLiteral("#d08a00"); // waiting to retry: amber
        else if (st == LogUploader::State::Problem)
            bg = QStringLiteral("#b03030"); // stopped: red
        b->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: white; border-radius: 3px; "
                                        "padding: 1px 6px; font-weight: bold; }")
                             .arg(bg));
        const QString base = b == m_qrzBtn ? tr("QRZ") : tr("LoTW");
        b->setText(waiting > 0 ? tr("%1 %2").arg(base).arg(waiting) : base);
        b->setToolTip(tip + tr("\nClick for Logging and uploads."));
    };
    paint(m_qrzBtn, m_uploader->qrzEnabled(), m_uploader->qrzState(), m_uploader->qrzWaiting(), m_uploader->qrzText());
    paint(m_lotwBtn,
          m_uploader->lotwEnabled(),
          m_uploader->lotwState(),
          m_uploader->lotwWaiting(),
          m_uploader->lotwText());
}

// RF frequency of the signal being decoded on the tab in use (dial + its audio offset);
// the dial if the tab has no single signal frequency.
qint64 MainWindow::currentSignalHz() const
{
    QWidget *w = m_tabs->currentWidget();
    const double audio = w == m_sstvPanel ? 0.0 : currentAudioHz();
    return audio > 0 ? rfHz(audio) : m_client->frequencyHz();
}

double MainWindow::currentAudioHz(double *halfWidthHz) const
{
    double audio = 0, half = 60;
    QWidget *w = m_tabs->currentWidget();
    if (w == m_cwPanel)
    {
        audio = m_cw->currentPitchHz();
        half = 40;
    }
    else if (w == m_rttyPanel)
    {
        audio = m_rtty->markHz() - m_rtty->shift() / 2.0;
        half = m_rtty->shift() / 2.0 + 40;
    }
    else if (w == m_pskPanel)
        audio = m_psk->carrierHz();
    else if (w == m_oliviaPanel)
    {
        audio = m_olivia->carrierHz();
        half = m_olivia->bandwidth() / 2.0;
    }
    else if (w == m_amtorPanel)
    {
        audio = m_amtor->carrierHz();
        half = 150;
    }
    else if (w == m_hellPanel)
    {
        audio = m_hell->carrierHz();
        half = 150; // Feld Hell is about 245 Hz wide
    }
    else if (w == m_navtexPanel)
    {
        audio = m_navtex->centreHz();
        half = 150;
    }
    else if (w == m_sstvPanel)
    {
        audio = 1900.0 + m_sstv->offsetHz(); // middle of the 1500..2300 Hz picture tones
        half = 420;
    }
    if (halfWidthHz)
        *halfWidthHz = half;
    return audio;
}

// SNR of the signal on the tab in use, from each receive spectrum line, smoothed over a few
// seconds; not while transmitting (the receive audio is then our own signal or nothing).
void MainWindow::trackSnr(const QVector<float> &db, double hzPerBin)
{
    if (anyTransmitting() || (m_client && m_client->radioTransmitting()))
        return;
    double half = 60;
    const double hz = currentAudioHz(&half);
    // RTTY: both mark and space must be there (the weaker one is measured).
    const double s = m_tabs->currentWidget() == m_rttyPanel
                         ? sigrep::snrTwoToneDb(db.constData(),
                                                int(db.size()),
                                                hzPerBin,
                                                double(m_rtty->markHz()),
                                                double(m_rtty->spaceHz()),
                                                40.0)
                         : sigrep::snrDb(db.constData(), int(db.size()), hzPerBin, hz, half);
    if (std::isnan(s))
        return;
    // A new signal frequency starts fresh rather than averaging two stations.
    if (std::isnan(m_snr) || std::abs(hz - m_snrHz) > half || !m_snrAge.isValid() || m_snrAge.elapsed() > 10000)
        m_snr = s;
    else
        m_snr += 0.15 * (s - m_snr);
    m_snrHz = hz;
    m_snrAge.start();
}

// Live readout in the top bar: the same measurement the QSO log uses, shown while it is
// fresh. Green from 10 dB (solid copy on most modes), amber 3-10 dB, red below 3 dB.
void MainWindow::updateSnrLabel()
{
    if (!m_snrLabel)
        return;
    QString text = tr("SNR --");
    QString style;
    QString tip;
    double half = 60;
    const double hz = currentAudioHz(&half);
    if (hz <= 0.0)
        tip = tr("SNR: this tab has no single signal frequency to measure.");
    else if (anyTransmitting() || (m_client && m_client->radioTransmitting()))
    {
        text = tr("SNR (TX)");
        tip = tr("SNR is measured while receiving.");
    }
    else if (!std::isnan(m_snr) && m_snrAge.isValid() && m_snrAge.elapsed() < 2000)
    {
        const int snr = int(std::lround(m_snr));
        if (m_snr < sigrep::kMinSnrDb)
            text = tr("SNR --"); // noise alone reads 0 +/- 3 dB: no signal to rate
        else
        {
            text = tr("SNR %1 dB").arg(snr);
            style = snr >= 10 ? QStringLiteral("color: #50d070; font-weight: bold;")
                              : QStringLiteral("color: #e0a030; font-weight: bold;");
        }
    }
    if (tip.isEmpty())
        tip = tr("Signal-to-noise of the signal at %1 Hz (+/- %2 Hz) on this tab: its strongest spectrum bin "
                 "against the typical strongest bin of same-width windows elsewhere in 300-2700 Hz, so noise "
                 "alone reads about 0 dB. RTTY measures the weaker of mark and space (both must be there). "
                 "Smoothed over about a second.\n"
                 "This is not WSJT-X's SNR (which is referred to a 2500 Hz bandwidth and reads much lower).\n"
                 "Green 10 dB and up, amber 3-10 dB; below 3 dB it shows -- (no signal above the noise) and the "
                 "QSO log leaves the SNR out of its signal report.")
                  .arg(qRound(hz))
                  .arg(qRound(half));
    if (m_snrLabel->text() != text)
        m_snrLabel->setText(text);
    if (m_snrLabel->styleSheet() != style)
        m_snrLabel->setStyleSheet(style);
    if (m_snrLabel->toolTip() != tip)
        m_snrLabel->setToolTip(tip);
}

void MainWindow::updateInputLevel()
{
    if (!m_inLevel)
        return;
    const double dt = m_inClock.isValid() ? std::clamp(m_inClock.restart() / 1000.0, 0.0, 2.0) : 0.3;
    if (!m_inClock.isValid())
        m_inClock.start();
    if (!m_client || m_client->state() == TciClient::State::Disconnected)
    {
        m_inLevel->setIdle(tr("--"));
        m_inHold.reset();
    }
    else if (anyTransmitting() || m_client->radioTransmitting())
    {
        // wfview sends silence for receive audio while transmitting
        m_inLevel->setIdle(tr("TX"));
        m_inHold.reset();
    }
    else if (m_inMeter.empty())
    {
        m_inLevel->setIdle(tr("no audio"));
        m_inHold.reset();
    }
    else
    {
        const double peak = m_inHold.update(m_inMeter.peakDb(), dt);
        m_inLevel->setLevel(m_inMeter.rmsDb(), peak);
    }
    m_inMeter.reset();
}

QString MainWindow::signalNote() const
{
    // Measured within the last minute of receiving (you may have transmitted since).
    const double snr = (m_snrAge.isValid() && m_snrAge.elapsed() < 60000) ? m_snr : std::nan("");
    return QString::fromStdString(sigrep::note(snr, m_client ? m_client->sMeterDbm() : std::nan("")));
}

void MainWindow::editCallLookup()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Callsign lookup"));
    auto *form = new QFormLayout(&dlg);
    auto *intro = new QLabel(tr("Fills in Name, QTH and Grid in the QSO log when you enter a call (only boxes that "
                                "are empty). <b>QRZ</b> uses your QRZ.com username and password (not the logbook "
                                "API key); without a QRZ XML subscription QRZ gives name and address but no grid. "
                                "<b>HamQTH</b> is free with a hamqth.com account."));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    form->addRow(intro);
    auto *svc = new QComboBox();
    svc->addItem(tr("Off"), int(CallLookup::Service::Off));
    svc->addItem(tr("QRZ.com (XML)"), int(CallLookup::Service::Qrz));
    svc->addItem(tr("HamQTH (free)"), int(CallLookup::Service::HamQth));
    svc->setCurrentIndex(svc->findData(int(m_lookup->service())));
    form->addRow(tr("Look up on"), svc);
    auto *user = new QLineEdit(m_lookup->user());
    user->setPlaceholderText(tr("your login name (usually your call)"));
    form->addRow(tr("Username"), user);
    auto *pass = new QLineEdit();
    pass->setEchoMode(QLineEdit::Password);
    const auto passHint = [pass, svc]()
    {
        const auto s = CallLookup::Service(svc->currentData().toInt());
        pass->setEnabled(s != CallLookup::Service::Off);
        pass->setPlaceholderText(s != CallLookup::Service::Off && SecretStore::has(CallLookup::secretName(s))
                                     ? tr("saved (type to change)")
                                     : tr("password"));
    };
    passHint();
    connect(svc, &QComboBox::currentIndexChanged, &dlg, passHint);
    form->addRow(tr("Password"), pass);
    auto *store = new QLabel(tr("The password is kept in Windows Credential Manager (encrypted under your Windows "
                                "login), never in a file. It is sent only to the callbook you choose, over HTTPS."));
    store->setWordWrap(true);
    form->addRow(store);
    auto *testRow = new QHBoxLayout();
    auto *test = new QPushButton(tr("Save and test"));
    auto *forget = new QPushButton(tr("Forget password"));
    testRow->addWidget(test);
    testRow->addWidget(forget);
    testRow->addStretch(1);
    form->addRow(testRow);
    auto *result = new QLabel(m_lookup->statusText());
    result->setWordWrap(true);
    form->addRow(result);
    const auto apply = [this, svc, user, pass]()
    {
        QString pw = pass->text();
        pass->clear();
        m_lookup->setConfig(CallLookup::Service(svc->currentData().toInt()), user->text(), pw); // wipes pw
    };
    connect(test,
            &QPushButton::clicked,
            &dlg,
            [this, apply, result, passHint]()
            {
                apply();
                passHint();
                result->setStyleSheet(QString());
                result->setText(tr("Testing..."));
                m_lookup->test(m_psk->myCall());
            });
    connect(forget,
            &QPushButton::clicked,
            &dlg,
            [this, passHint, result]()
            {
                m_lookup->forgetPassword();
                passHint();
                result->setText(m_lookup->statusText());
            });
    connect(m_lookup,
            &CallLookup::testResult,
            &dlg,
            [result](bool pass, const QString &text)
            {
                result->setStyleSheet(pass ? QStringLiteral("color: white; background: #2e8b3a; padding: 2px;")
                                           : QStringLiteral("color: white; background: #b03030; padding: 2px;"));
                result->setText(text);
            });
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() == QDialog::Accepted
        && (CallLookup::Service(svc->currentData().toInt()) != m_lookup->service()
            || user->text().trimmed() != m_lookup->user() || !pass->text().isEmpty()))
        apply();
    QString leftover = pass->text();
    wipe(leftover);
    pass->clear();
}

// The sub-mode of the tab in use, as the PSK Reporter / ADIF mode needs it.
QString MainWindow::currentSubMode() const
{
    QWidget *w = m_tabs->currentWidget();
    if (w == m_pskPanel)
        return QString::fromLatin1(psk::speedName(m_psk->speed()));
    if (w == m_oliviaPanel)
        return m_olivia->modeName();
    return QString();
}
