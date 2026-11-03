#include "MainWindow.h"

#include "CwDecoder.h"
#include "CwPanel.h"
#include "RttyEngine.h"
#include "RttyPanel.h"
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
    setWindowTitle(tr("TCI CW/RTTY %1").arg(QStringLiteral(APP_VERSION)));

    m_client = new TciClient(this);
    m_cw = new CwDecoder(this);
    m_rtty = new RttyEngine(this);
    m_rtty->loadSettings(m_settings);

    // TX audio comes from the RTTY modulator; it renders silence when idle.
    std::shared_ptr<RttyTxSource> tx = m_rtty->txSource();
    m_client->setTxRender([tx](float *out, size_t frames, int channels, float rate)
                          { tx->render(out, frames, channels, rate); });

    auto *central = new QWidget();
    auto *v = new QVBoxLayout(central);
    v->addWidget(buildConnectionBar());

    m_tabs = new QTabWidget();
    m_cwPanel = new CwPanel(m_cw, m_settings);
    m_rttyPanel = new RttyPanel(m_rtty, m_settings);
    m_tabs->addTab(m_cwPanel, tr("CW"));
    m_tabs->addTab(m_rttyPanel, tr("RTTY"));
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
    connect(m_client, &TciClient::diagnostics, m_diag, &QLabel::setText);
    connect(m_client, &TciClient::logLine, this, &MainWindow::log);
    connect(m_rtty,
            &RttyEngine::transmitRequested,
            this,
            [this](bool on)
            {
                m_client->setTransmit(on);
                log(on ? tr("TX requested (trx:0,true)") : tr("TX released (trx:0,false)"));
            });

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
    m_mode->setToolTip(tr("Radio mode. RTTY transmit needs DIGU or DIGL (USB-D / LSB-D)."));
    m_mode->setMinimumContentsLength(5);
    h->addWidget(m_mode);
    m_txLamp = new QLabel(tr(" RX "));
    m_txLamp->setAlignment(Qt::AlignCenter);
    m_txLamp->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("  TX  ")) + 8);
    h->addWidget(m_txLamp);

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
                            tr("About TCI CW/RTTY"),
                            tr("<b>TCI CW/RTTY %1</b><br><br>"
                               "Software CW decoder and RTTY modem that works with wfview (or any TCI "
                               "server) over TCI: receive audio, transmit audio, PTT, frequency and mode.<br><br>"
                               "CW decoding uses ggmorse (MIT) and a hand-key decoder; RTTY uses a "
                               "mark/space filter demodulator with W7AY optimal ATC.<br><br>"
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
    m_cw->setPaused(on); // do not decode our own sidetone / monitor
    m_txLamp->setText(on ? tr(" TX ") : tr(" RX "));
    m_txLamp->setStyleSheet(on ? QStringLiteral("background-color: #c02020; color: white; font-weight: bold;")
                               : QString());
}

void MainWindow::updateRadioState()
{
    const bool ready = m_client->isReady();
    const bool canTx = ready && !m_client->receiveOnly();
    m_rtty->setConnected(ready, canTx);
    m_rttyPanel->setRadioState(ready, canTx, m_client->modulation());
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
    m_wantConnected = false;
    m_client->disconnectFromServer();
    saveAll();
    QMainWindow::closeEvent(event);
}
