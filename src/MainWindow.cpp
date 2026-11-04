#include "MainWindow.h"

#include "CwDecoder.h"
#include "CwPanel.h"
#include "CwTxEngine.h"
#include "HellEngine.h"
#include "HellPanel.h"
#include "PskEngine.h"
#include "PskPanel.h"
#include "RttyEngine.h"
#include "RttyPanel.h"
#include "SpectrumEngine.h"
#include "TxPowerWatch.h"
#include "modemtxsource.h"
#include "rttytxsource.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTabWidget>
#include <QVBoxLayout>

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

MainWindow::MainWindow(QSettings *settings, QWidget *parent)
    : QMainWindow(parent)
    , m_settings(settings)
{
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
    m_spectrum = new SpectrumEngine(this);
    m_modems = {m_psk, m_hell, m_cwTx};

    // TX audio comes from whichever mode is transmitting (only one can: see
    // onTransmitRequested). RTTY renders silence when idle.
    std::shared_ptr<RttyTxSource> rttyTx = m_rtty->txSource();
    std::vector<std::shared_ptr<ModemTxSource>> modemTx;
    for (ModemEngine *e : m_modems)
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

    m_tabs = new QTabWidget();
    m_cwPanel = new CwPanel(m_cw, m_cwTx, m_settings);
    m_rttyPanel = new RttyPanel(m_rtty, m_settings);
    m_pskPanel = new PskPanel(m_psk, m_settings);
    m_hellPanel = new HellPanel(m_hell, m_settings);
    m_tabs->addTab(m_cwPanel, tr("CW"));
    m_tabs->addTab(m_rttyPanel, tr("RTTY"));
    m_tabs->addTab(m_pskPanel, tr("PSK"));
    m_tabs->addTab(m_hellPanel, tr("Hell"));
    v->addWidget(m_tabs, 1);
    setCentralWidget(central);

    m_diag = new QLabel(tr("Not connected"));
    m_diag->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(m_diag, 1);

    buildLogDock();
    buildMenus();

    // ---- wiring
    connect(m_client, &TciClient::stateChanged, this, &MainWindow::onStateChanged);
    connect(m_client, &TciClient::radioInfoChanged, this, &MainWindow::onRadioInfo);
    connect(m_client, &TciClient::frequencyChanged, this, &MainWindow::onFrequency);
    connect(m_client, &TciClient::modulationChanged, this, &MainWindow::onModulation);
    connect(m_client, &TciClient::radioTransmitChanged, this, &MainWindow::onRadioTransmit);
    connect(m_client, &TciClient::rxAudio, m_cw, &CwDecoder::pushAudio);
    connect(m_client, &TciClient::rxAudio, m_rtty, &RttyEngine::pushAudio);
    connect(m_client, &TciClient::rxAudio, m_psk, &ModemEngine::pushAudio);
    connect(m_client, &TciClient::rxAudio, m_hell, &ModemEngine::pushAudio);
    connect(m_client, &TciClient::rxAudio, m_spectrum, &SpectrumEngine::pushAudio);
    connect(m_spectrum, &SpectrumEngine::line, m_pskPanel, &ModemPanel::addSpectrumLine);
    connect(m_spectrum, &SpectrumEngine::line, m_hellPanel, &ModemPanel::addSpectrumLine);
    connect(m_client, &TciClient::diagnostics, m_diag, &QLabel::setText);
    m_powerWatch = new TxPowerWatch(this);
    connect(m_client, &TciClient::txSensors, m_powerWatch, &TxPowerWatch::onSensors);
    connect(m_client, &TciClient::radioTransmitChanged, m_powerWatch, &TxPowerWatch::setRadioTransmitting);
    connect(m_powerWatch, &TxPowerWatch::readout, m_txMeter, &QLabel::setText);
    connect(m_powerWatch,
            &TxPowerWatch::noPower,
            this,
            [this]()
            {
                m_txHint->show();
                log(tr("Radio transmitted with no output power: check its data mode and DATA MOD input (LAN/USB)."));
            });
    connect(m_client, &TciClient::logLine, this, &MainWindow::log);
    connect(m_rtty, &RttyEngine::transmitRequested, this, [this](bool on) { onTransmitRequested(m_rtty, on); });
    for (ModemEngine *e : m_modems)
        connect(e, &ModemEngine::transmitRequested, this, [this, e](bool on) { onTransmitRequested(e, on); });

    // One "My call" for every mode.
    const auto shareCall = [this](const QString &c)
    {
        m_rtty->setMyCall(c);
        for (ModemEngine *e : m_modems)
            e->setMyCall(c);
    };
    // Start from the shared value (the new modes read Station/MyCall, falling back to 0.1.0's RTTY/MyCall).
    shareCall(m_psk->myCall().isEmpty() ? m_rtty->myCall() : m_psk->myCall());
    connect(m_rtty, &RttyEngine::settingsChanged, this, [this, shareCall]() { shareCall(m_rtty->myCall()); });
    for (ModemEngine *e : m_modems)
        connect(e, &ModemEngine::settingsChanged, this, [e, shareCall]() { shareCall(e->myCall()); });

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
    restoreGeometry(m_settings->value(QStringLiteral("Geometry")).toByteArray());
    restoreState(m_settings->value(QStringLiteral("State")).toByteArray());
    m_tabs->setCurrentIndex(m_settings->value(QStringLiteral("Tab"), 0).toInt());
    m_settings->endGroup();
    if (m_settings->value(QStringLiteral("TCI/ConnectOnStart"), false).toBool())
        QTimer::singleShot(0, this, &MainWindow::toggleConnection);

    updateRadioState();
}

MainWindow::~MainWindow() = default;

QWidget *MainWindow::buildTxHint()
{
    m_txHint = new QWidget();
    m_txHint->setObjectName(QStringLiteral("txHint"));
    m_txHint->setStyleSheet(QStringLiteral("#txHint { background-color: #5a4310; border-radius: 3px; }"));
    auto *h = new QHBoxLayout(m_txHint);
    h->setContentsMargins(8, 4, 4, 4);
    auto *text = new QLabel(
        tr("<b>The radio keyed up but reported no output power.</b> The transmit audio reaches the radio, but "
           "not through the input the radio is using. Check that the radio's <b>data mode</b> (Data 1/2/3 in "
           "wfview) uses the <b>DATA MOD input</b> that matches how wfview connects to it, LAN or USB. For "
           "example, on an IC-7610 with DATA1 = USB and DATA2 = LAN, use Data 2 when wfview connects over LAN. "
           "Also check the TX level, and that the radio is in a data or SSB mode."));
    text->setWordWrap(true);
    text->setStyleSheet(QStringLiteral("color: #ffe7b0;"));
    h->addWidget(text, 1);
    auto *close = new QPushButton(QStringLiteral("\u2715"));
    close->setFlat(true);
    close->setToolTip(tr("Hide"));
    close->setFixedWidth(close->sizeHint().height());
    h->addWidget(close, 0, Qt::AlignTop);
    connect(close, &QPushButton::clicked, m_txHint, &QWidget::hide);
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

    m_freq = new QLineEdit();
    m_freq->setAlignment(Qt::AlignRight);
    m_freq->setToolTip(tr("Receiver frequency in Hz. Type a new one (e.g. 14.080 or 14080k) and press Enter."));
    QFont ff = m_freq->font();
    ff.setPointSizeF(ff.pointSizeF() * 1.3);
    ff.setBold(true);
    m_freq->setFont(ff);
    m_freq->setMinimumWidth(QFontMetrics(ff).horizontalAdvance(QStringLiteral("888.888.888")) + 16);
    m_freq->setMaximumWidth(QFontMetrics(ff).horizontalAdvance(QStringLiteral("888.888.888")) + 24);
    h->addWidget(m_freq);
    m_mode = new QComboBox();
    m_mode->setToolTip(tr("Radio mode. Every transmit mode here sends audio, so it needs a data mode (DIGU/DIGL) "
                          "or USB/LSB.\nNote: choosing DIGU/DIGL here makes wfview use Data 1. If your radio's "
                          "Data 1 input is not the one wfview feeds (LAN or USB), set the data mode in wfview instead."));
    m_mode->setMinimumContentsLength(5);
    h->addWidget(m_mode);
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
                m_freq->setText(formatFreq(m_client->frequencyHz()));
                m_freq->clearFocus();
            });
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

    QMenu *opts = menuBar()->addMenu(tr("&Options"));
    QAction *onStart = opts->addAction(tr("Connect on start"));
    onStart->setCheckable(true);
    onStart->setChecked(m_settings->value(QStringLiteral("TCI/ConnectOnStart"), false).toBool());
    connect(onStart,
            &QAction::toggled,
            this,
            [this](bool on) { m_settings->setValue(QStringLiteral("TCI/ConnectOnStart"), on); });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
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
                               "Hell: Feld Hell (Hellschreiber) send and painted receive.<br><br>"
                               "License: GPLv3.")
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
        m_connState->setText(tr("Connecting..."));
        break;
    case TciClient::State::Connected:
        m_connect->setText(tr("Disconnect"));
        m_connState->setText(tr("Connected, waiting for the radio..."));
        break;
    case TciClient::State::Ready:
        m_connect->setText(tr("Disconnect"));
        m_connState->setText(tr("%1  (%2)").arg(m_client->device(), m_client->protocol()));
        if (m_client->frequencyHz() > 0)
            m_freq->setText(formatFreq(m_client->frequencyHz()));
        break;
    }
    m_connState->setToolTip(m_connState->text());
    m_host->setEnabled(state == TciClient::State::Disconnected);
    m_port->setEnabled(state == TciClient::State::Disconnected);
    updateRadioState();
}

void MainWindow::onRadioInfo()
{
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

void MainWindow::onFrequency(qint64 hz)
{
    if (!m_freq->hasFocus())
        m_freq->setText(formatFreq(hz));
}

void MainWindow::onModulation(const QString &mode)
{
    const QSignalBlocker b(m_mode);
    if (m_mode->findText(mode) < 0)
        m_mode->addItem(mode);
    m_mode->setCurrentText(mode);
    m_rtty->setRadioMode(mode);
    updateRadioState();
}

void MainWindow::onRadioTransmit(bool on)
{
    m_rtty->setRadioTransmitting(on);
    for (ModemEngine *e : m_modems)
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
        for (ModemEngine *e : m_modems)
            if (who != e && e->transmitting())
                e->abortTx();
    }
    else if (anyTransmitting())
    {
        return; // another mode has taken over; keep PTT
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
    for (ModemEngine *e : m_modems)
        if (e->transmitting())
            return true;
    return false;
}

void MainWindow::updateRadioState()
{
    const bool ready = m_client->isReady();
    const bool canTx = ready && !m_client->receiveOnly();
    m_rtty->setConnected(ready, canTx);
    m_rttyPanel->setRadioState(ready, canTx, m_client->modulation());
    for (ModemEngine *e : m_modems)
        e->setConnected(ready, canTx);
    m_cwPanel->setRadioState(ready, canTx, m_client->modulation());
    m_pskPanel->setRadioState(ready, canTx, m_client->modulation());
    m_hellPanel->setRadioState(ready, canTx, m_client->modulation());
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
    m_settings->beginGroup(QStringLiteral("Window"));
    m_settings->setValue(QStringLiteral("Geometry"), saveGeometry());
    m_settings->setValue(QStringLiteral("State"), saveState());
    m_settings->setValue(QStringLiteral("Tab"), m_tabs->currentIndex());
    m_settings->endGroup();
    m_settings->setValue(QStringLiteral("TCI/AutoReconnect"), m_autoReconnect->isChecked());
    m_settings->sync();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_rtty->transmitting())
        m_rtty->abortTx();
    for (ModemEngine *e : m_modems)
        if (e->transmitting())
            e->abortTx();
    m_wantConnected = false;
    m_client->disconnectFromServer();
    saveAll();
    QMainWindow::closeEvent(event);
}
