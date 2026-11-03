#include "RttyPanel.h"

#include "RttyEngine.h"
#include "ScopeWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int maxRxChars = 200000;

QFont monoFont()
{
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    return f;
}

QProgressBar *makeBar()
{
    auto *b = new QProgressBar();
    b->setRange(0, 100);
    b->setTextVisible(false);
    b->setMaximumHeight(12);
    b->setMinimumWidth(60);
    return b;
}
} // namespace

RttyPanel::RttyPanel(RttyEngine *engine, QSettings *settings, QWidget *parent)
    : QWidget(parent)
    , m_rtty(engine)
    , m_settings(settings)
{
    auto *root = new QVBoxLayout(this);
    m_split = new QSplitter(Qt::Vertical);
    m_split->setChildrenCollapsible(false);

    m_split->addWidget(buildTop());

    m_rx = new QPlainTextEdit();
    m_rx->setReadOnly(true);
    m_rx->setFont(monoFont());
    m_rx->setMinimumHeight(60);
    m_rx->setToolTip(tr("Double-click a call sign to copy it to the Call field."));
    m_rx->viewport()->installEventFilter(this);
    m_split->addWidget(m_rx);

    m_split->addWidget(buildTx());
    m_split->setStretchFactor(0, 0);
    m_split->setStretchFactor(1, 1);
    m_split->setStretchFactor(2, 0);
    root->addWidget(m_split);

    connect(m_rtty, &RttyEngine::settingsChanged, this, &RttyPanel::syncSettings);
    connect(m_rtty, &RttyEngine::macrosChanged, this, &RttyPanel::syncMacros);
    connect(m_rtty, &RttyEngine::statsChanged, this, &RttyPanel::syncStats);
    connect(m_rtty, &RttyEngine::statusChanged, m_status, &QLabel::setText);
    connect(m_rtty, &RttyEngine::transmittingChanged, this, &RttyPanel::syncTransmitting);
    connect(m_rtty, &RttyEngine::textAppended, this, &RttyPanel::appendText);
    connect(m_rtty, &RttyEngine::scopePoints, m_scope, &ScopeWidget::addPoints);

    // F1-F8 send macros, Esc aborts, while this tab is showing.
    for (int i = 0; i < RttyEngine::kMacroCount; ++i)
    {
        auto *sc = new QShortcut(QKeySequence(Qt::Key_F1 + i), this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, [this, i]() { m_rtty->sendMacro(i); });
    }
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, m_rtty, &RttyEngine::abortTx);

    // Default proportions: settings/scope at their natural height, the transmit
    // area about five lines of text, and the receive pane gets the rest.
    const QByteArray saved = m_settings ? m_settings->value(QStringLiteral("RTTY/Split")).toByteArray() : QByteArray();
    if (saved.isEmpty() || !m_split->restoreState(saved))
    {
        const int line = QFontMetrics(monoFont()).lineSpacing();
        const int top = m_split->widget(0)->sizeHint().height();
        const int txArea = m_split->widget(2)->minimumSizeHint().height() + 3 * line;
        m_split->setSizes({top, 30 * line, txArea});
    }

    syncSettings();
    syncMacros();
    syncStats();
    syncTransmitting(false);
    m_status->setText(m_rtty->status());
}

QWidget *RttyPanel::buildTop()
{
    auto *top = new QWidget();
    auto *h = new QHBoxLayout(top);
    h->setContentsMargins(0, 0, 0, 0);

    auto *left = new QVBoxLayout();

    // row: decode controls
    auto *r1 = new QHBoxLayout();
    m_decode = new QPushButton(tr("Decode"));
    m_decode->setCheckable(true);
    r1->addWidget(m_decode);
    r1->addWidget(new QLabel(tr("Squelch")));
    m_squelch = new QSlider(Qt::Horizontal);
    m_squelch->setRange(0, 95);
    m_squelch->setMinimumWidth(70);
    m_squelch->setToolTip(tr("Tone-contrast squelch. Noise reads up to about 0.44, a usable signal above 0.5."));
    r1->addWidget(m_squelch, 1);
    m_squelchValue = new QLabel();
    m_squelchValue->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("0.00")) + 4);
    r1->addWidget(m_squelchValue);
    auto *tune = new QPushButton(tr("Auto-tune"));
    tune->setToolTip(tr("Find the mark/space pair in the last few seconds of audio and set Mark to it."));
    r1->addWidget(tune);
    auto *resync = new QPushButton(tr("Re-sync"));
    r1->addWidget(resync);
    auto *clear = new QPushButton(tr("Clear"));
    r1->addWidget(clear);
    m_reverse = new QPushButton(tr("Reverse"));
    m_reverse->setCheckable(true);
    m_reverse->setToolTip(tr("Swap mark and space (for stations sending inverted)."));
    r1->addWidget(m_reverse);
    left->addLayout(r1);

    // grid: modem settings (two label/field pairs per row; columns stretch evenly)
    auto *g = new QGridLayout();
    int row = 0;
    m_baud = new QComboBox();
    m_baud->setEditable(true);
    m_baud->addItems({QStringLiteral("45.45"), QStringLiteral("50"), QStringLiteral("75"), QStringLiteral("100")});
    m_baud->lineEdit()->setValidator(new QDoubleValidator(20.0, 300.0, 2, m_baud));
    m_shift = new QComboBox();
    m_shift->setEditable(true);
    m_shift->addItems({QStringLiteral("170"), QStringLiteral("200"), QStringLiteral("425"), QStringLiteral("850")});
    m_shift->lineEdit()->setValidator(new QIntValidator(20, 1000, m_shift));
    g->addWidget(new QLabel(tr("Baud")), row, 0);
    g->addWidget(m_baud, row, 1);
    g->addWidget(new QLabel(tr("Shift")), row, 2);
    g->addWidget(m_shift, row, 3);
    ++row;
    m_mark = new QSpinBox();
    m_mark->setRange(200, 3500);
    m_mark->setSuffix(tr(" Hz"));
    m_space = new QLabel();
    g->addWidget(new QLabel(tr("Mark")), row, 0);
    g->addWidget(m_mark, row, 1);
    g->addWidget(new QLabel(tr("Space")), row, 2);
    g->addWidget(m_space, row, 3);
    ++row;
    m_sideband = new QComboBox();
    m_sideband->addItem(tr("Auto"), RttyEngine::SidebandAuto);
    m_sideband->addItem(tr("USB"), RttyEngine::SidebandUsb);
    m_sideband->addItem(tr("LSB"), RttyEngine::SidebandLsb);
    m_sideband->setToolTip(
        tr("Auto follows the radio's mode: USB/DIGU puts mark on the higher tone, LSB/DIGL on the lower."));
    m_stopBits = new QComboBox();
    m_stopBits->addItem(QStringLiteral("1"), 1.0);
    m_stopBits->addItem(QStringLiteral("1.5"), 1.5);
    m_stopBits->addItem(QStringLiteral("2"), 2.0);
    g->addWidget(new QLabel(tr("Sideband")), row, 0);
    g->addWidget(m_sideband, row, 1);
    g->addWidget(new QLabel(tr("Stop bits")), row, 2);
    g->addWidget(m_stopBits, row, 3);
    ++row;
    m_usos = new QCheckBox(tr("USOS"));
    m_usos->setToolTip(tr("Unshift on space: return to letters after a space (RX and TX)."));
    m_figures = new QComboBox();
    m_figures->addItem(tr("US TTY"), false);
    m_figures->addItem(tr("ITA2"), true);
    g->addWidget(m_usos, row, 0, 1, 2);
    g->addWidget(new QLabel(tr("Figures")), row, 2);
    g->addWidget(m_figures, row, 3);
    ++row;
    m_myCall = new QLineEdit();
    m_myCall->setPlaceholderText(tr("your call"));
    m_myCall->setToolTip(tr("Your own call sign, used by {MYCALL} in the macros."));
    g->addWidget(new QLabel(tr("My call")), row, 0);
    g->addWidget(m_myCall, row, 1, 1, 3);
    g->setColumnStretch(1, 1);
    g->setColumnStretch(3, 1);
    left->addLayout(g);

    // level bars
    auto *bars = new QGridLayout();
    m_markBar = makeBar();
    m_spaceBar = makeBar();
    m_qualityBar = makeBar();
    bars->addWidget(new QLabel(tr("Mark")), 0, 0);
    bars->addWidget(m_markBar, 0, 1);
    bars->addWidget(new QLabel(tr("Space")), 1, 0);
    bars->addWidget(m_spaceBar, 1, 1);
    bars->addWidget(new QLabel(tr("Quality")), 2, 0);
    bars->addWidget(m_qualityBar, 2, 1);
    bars->setColumnStretch(1, 1);
    left->addLayout(bars);

    m_modeWarning = new QLabel();
    m_modeWarning->setWordWrap(true);
    m_modeWarning->setStyleSheet(QStringLiteral("color: #e0a030;"));
    m_modeWarning->setVisible(false);
    left->addWidget(m_modeWarning);

    m_status = new QLabel();
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    left->addWidget(m_status);
    left->addStretch(1);

    h->addLayout(left, 3);
    m_scope = new ScopeWidget();
    m_scope->setToolTip(tr("Tuning scope: a clean \"+\" means mark and space are on frequency."));
    h->addWidget(m_scope, 2);

    // wiring
    connect(m_decode, &QPushButton::toggled, m_rtty, &RttyEngine::setDecoding);
    connect(m_decode, &QPushButton::toggled, this, &RttyPanel::updateScopeIdle);
    connect(m_squelch, &QSlider::valueChanged, this, [this](int v) { m_rtty->setSquelch(v / 100.0); });
    connect(tune, &QPushButton::clicked, m_rtty, &RttyEngine::autoTune);
    connect(resync, &QPushButton::clicked, m_rtty, &RttyEngine::resync);
    connect(clear, &QPushButton::clicked, this, [this]() { m_rx->clear(); });
    connect(m_reverse, &QPushButton::toggled, m_rtty, &RttyEngine::setReverse);
    connect(m_baud->lineEdit(),
            &QLineEdit::editingFinished,
            this,
            [this]() { m_rtty->setBaud(m_baud->currentText().toDouble()); });
    connect(m_baud, &QComboBox::activated, this, [this](int) { m_rtty->setBaud(m_baud->currentText().toDouble()); });
    connect(m_shift->lineEdit(),
            &QLineEdit::editingFinished,
            this,
            [this]() { m_rtty->setShift(m_shift->currentText().toInt()); });
    connect(m_shift, &QComboBox::activated, this, [this](int) { m_rtty->setShift(m_shift->currentText().toInt()); });
    connect(m_mark, &QSpinBox::valueChanged, m_rtty, &RttyEngine::setMarkHz);
    connect(m_sideband,
            &QComboBox::activated,
            this,
            [this](int i) { m_rtty->setSideband(m_sideband->itemData(i).toInt()); });
    connect(m_stopBits,
            &QComboBox::activated,
            this,
            [this](int i) { m_rtty->setStopBits(m_stopBits->itemData(i).toDouble()); });
    connect(m_usos, &QCheckBox::toggled, m_rtty, &RttyEngine::setUsos);
    connect(
        m_figures, &QComboBox::activated, this, [this](int i) { m_rtty->setIta2(m_figures->itemData(i).toBool()); });
    connect(m_myCall, &QLineEdit::editingFinished, this, [this]() { m_rtty->setMyCall(m_myCall->text()); });
    return top;
}

QWidget *RttyPanel::buildTx()
{
    auto *w = new QWidget();
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    // Call + macro buttons. The buttons sit in a 4-column grid so they wrap
    // into two rows instead of forcing a minimum window width.
    auto *macroRow = new QHBoxLayout();
    auto *callBox = new QVBoxLayout();
    callBox->addWidget(new QLabel(tr("Call")));
    m_call = new QLineEdit();
    m_call->setPlaceholderText(tr("their call"));
    m_call->setToolTip(
        tr("The other station's call, used by {CALL}. Double-click a call in the receive pane to fill it in."));
    m_call->setMaximumWidth(160);
    callBox->addWidget(m_call);
    callBox->addStretch(1);
    macroRow->addLayout(callBox);

    auto *grid = new QGridLayout();
    for (int i = 0; i < RttyEngine::kMacroCount; ++i)
    {
        auto *b = new QPushButton();
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(b, &QPushButton::clicked, this, [this, i]() { m_rtty->sendMacro(i); });
        grid->addWidget(b, i / 4, i % 4);
        m_macroButtons << b;
    }
    macroRow->addLayout(grid, 1);
    auto *edit = new QPushButton(tr("Edit..."));
    edit->setToolTip(tr("Edit the macro names and texts. {MYCALL} and {CALL} are filled in when sent."));
    macroRow->addWidget(edit, 0, Qt::AlignTop);
    v->addLayout(macroRow);

    m_txText = new QPlainTextEdit();
    m_txText->setFont(monoFont());
    m_txText->setPlaceholderText(
        tr("Type here, then Send (Ctrl+Enter). Text sent while transmitting is added to the queue."));
    m_txText->setMinimumHeight(QFontMetrics(monoFont()).lineSpacing() * 2 + 12);
    m_txText->installEventFilter(this);
    v->addWidget(m_txText, 1);

    auto *btns = new QHBoxLayout();
    m_send = new QPushButton(tr("Send"));
    m_send->setToolTip(tr("Send the typed text (keys up first if needed, releases when done unless Hold TX)."));
    m_txBtn = new QPushButton(tr("TX"));
    m_txBtn->setToolTip(tr("Key up and send mark (or diddle) until RX."));
    m_rxBtn = new QPushButton(tr("RX"));
    m_rxBtn->setToolTip(tr("Release after everything queued has been sent."));
    m_abort = new QPushButton(tr("Abort"));
    m_abort->setToolTip(tr("Stop transmitting now (Esc)."));
    m_hold = new QCheckBox(tr("Hold TX"));
    m_hold->setToolTip(tr("Stay keyed between sends until RX is pressed."));
    m_diddle = new QCheckBox(tr("Diddle"));
    m_diddle->setToolTip(tr("Send LTRS while waiting for text, instead of a steady mark."));
    btns->addWidget(m_send);
    btns->addWidget(m_txBtn);
    btns->addWidget(m_rxBtn);
    btns->addWidget(m_abort);
    btns->addWidget(m_hold);
    btns->addWidget(m_diddle);
    btns->addStretch(1);
    btns->addWidget(new QLabel(tr("TX level")));
    m_txLevel = new QSlider(Qt::Horizontal);
    m_txLevel->setRange(0, 100);
    m_txLevel->setMinimumWidth(80);
    m_txLevel->setMaximumWidth(200);
    m_txLevel->setToolTip(
        tr("AFSK tone level sent to wfview. Set the radio's ALC/power with this and wfview's TX gain."));
    btns->addWidget(m_txLevel, 1);
    m_txLevelValue = new QLabel();
    m_txLevelValue->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("100%")) + 4);
    btns->addWidget(m_txLevelValue);
    v->addLayout(btns);

    connect(m_call, &QLineEdit::textEdited, m_rtty, &RttyEngine::setCall);
    connect(edit, &QPushButton::clicked, this, &RttyPanel::editMacros);
    connect(m_send, &QPushButton::clicked, this, &RttyPanel::sendTyped);
    connect(m_txBtn, &QPushButton::clicked, m_rtty, &RttyEngine::startTx);
    connect(m_rxBtn, &QPushButton::clicked, m_rtty, &RttyEngine::finishTx);
    connect(m_abort, &QPushButton::clicked, m_rtty, &RttyEngine::abortTx);
    connect(m_hold, &QCheckBox::toggled, m_rtty, &RttyEngine::setHoldTx);
    connect(m_diddle, &QCheckBox::toggled, m_rtty, &RttyEngine::setDiddle);
    connect(m_txLevel, &QSlider::valueChanged, this, [this](int v) { m_rtty->setTxLevel(v / 100.0); });
    return w;
}

void RttyPanel::saveState()
{
    if (m_settings)
        m_settings->setValue(QStringLiteral("RTTY/Split"), m_split->saveState());
}

void RttyPanel::setRadioState(bool ready, bool canTransmit, const QString &mode)
{
    m_ready = ready;
    m_canTx = ready && canTransmit;
    m_mode = mode;
    syncTransmitting(m_rtty->transmitting());
    updateModeWarning();
}

void RttyPanel::updateModeWarning()
{
    const QString m = m_mode.toUpper();
    QString warn;
    if (m_ready && !m.isEmpty() && m != QLatin1String("DIGU") && m != QLatin1String("DIGL") && m != QLatin1String("USB")
        && m != QLatin1String("LSB"))
        warn = tr("Radio is in %1. RTTY needs an SSB or data mode (DIGU/DIGL = USB-D/LSB-D).").arg(m);
    else if (m_ready && (m == QLatin1String("USB") || m == QLatin1String("LSB")))
        warn = tr("Radio is in %1 (not data mode): transmit audio may come from the microphone path. "
                  "Use %2 for AFSK.")
                   .arg(m, m == QLatin1String("USB") ? QStringLiteral("DIGU") : QStringLiteral("DIGL"));
    m_modeWarning->setText(warn);
    m_modeWarning->setVisible(!warn.isEmpty());
}

void RttyPanel::syncSettings()
{
    const QSignalBlocker b1(m_decode), b2(m_squelch), b3(m_reverse), b4(m_baud), b5(m_shift), b6(m_mark),
        b7(m_sideband), b8(m_stopBits), b9(m_usos), b10(m_figures), b11(m_hold), b12(m_diddle), b13(m_txLevel);
    m_decode->setChecked(m_rtty->decoding());
    m_squelch->setValue(qRound(m_rtty->squelch() * 100));
    m_squelchValue->setText(QString::number(m_rtty->squelch(), 'f', 2));
    m_reverse->setChecked(m_rtty->reverse());
    if (!m_baud->lineEdit()->hasFocus())
        m_baud->setCurrentText(QString::number(m_rtty->baud(), 'g', 5));
    if (!m_shift->lineEdit()->hasFocus())
        m_shift->setCurrentText(QString::number(m_rtty->shift()));
    m_mark->setValue(m_rtty->markHz());
    const QString sb = m_rtty->sideband() == RttyEngine::SidebandAuto
        ? (m_rtty->usbEffective() ? tr("  (USB)") : tr("  (LSB)"))
        : QString();
    m_space->setText(tr("%1 Hz%2").arg(m_rtty->spaceHz()).arg(sb));
    m_sideband->setCurrentIndex(m_sideband->findData(m_rtty->sideband()));
    m_stopBits->setCurrentIndex(m_stopBits->findData(m_rtty->stopBits()));
    m_usos->setChecked(m_rtty->usos());
    m_figures->setCurrentIndex(m_rtty->ita2() ? 1 : 0);
    if (!m_myCall->hasFocus())
        m_myCall->setText(m_rtty->myCall());
    if (!m_call->hasFocus())
        m_call->setText(m_rtty->call());
    m_hold->setChecked(m_rtty->holdTx());
    m_diddle->setChecked(m_rtty->diddle());
    m_txLevel->setValue(qRound(m_rtty->txLevel() * 100));
    m_txLevelValue->setText(QStringLiteral("%1%").arg(qRound(m_rtty->txLevel() * 100)));
    updateScopeIdle();
}

void RttyPanel::syncMacros()
{
    const QStringList names = m_rtty->macroNames();
    const QStringList texts = m_rtty->macroTexts();
    for (int i = 0; i < m_macroButtons.size() && i < names.size(); ++i)
    {
        m_macroButtons[i]->setText(QStringLiteral("F%1 %2").arg(i + 1).arg(names[i]));
        m_macroButtons[i]->setToolTip(texts.value(i).trimmed());
    }
}

void RttyPanel::syncStats()
{
    // 0..40 dB over noise fills the level bars.
    const auto pct = [](double db) { return std::clamp(int(std::lround(db * 2.5)), 0, 100); };
    m_markBar->setValue(pct(m_rtty->markDb()));
    m_spaceBar->setValue(pct(m_rtty->spaceDb()));
    m_qualityBar->setValue(std::clamp(int(std::lround(m_rtty->quality() * 100)), 0, 100));
    m_qualityBar->setToolTip(tr("Quality %1 (squelch %2)")
                                 .arg(m_rtty->quality(), 0, 'f', 2)
                                 .arg(m_rtty->squelchOpen() ? tr("open") : tr("closed")));
}

void RttyPanel::syncTransmitting(bool on)
{
    m_send->setEnabled(m_canTx);
    m_txBtn->setEnabled(m_canTx && !on);
    m_rxBtn->setEnabled(on);
    m_abort->setEnabled(on);
    for (QPushButton *b : m_macroButtons)
        b->setEnabled(m_canTx);
    m_txBtn->setStyleSheet(on ? QStringLiteral("background-color: #a02020; color: white;") : QString());
    updateScopeIdle();
}

void RttyPanel::updateScopeIdle()
{
    if (m_rtty->transmitting())
        m_scope->setIdleText(tr("TX"));
    else if (!m_rtty->decoding())
        m_scope->setIdleText(tr("Decode off"));
    else
        m_scope->setIdleText(QString());
}

void RttyPanel::appendText(const QString &text)
{
    QScrollBar *sb = m_rx->verticalScrollBar();
    const bool atEnd = sb->value() >= sb->maximum() - 2;
    m_rx->moveCursor(QTextCursor::End);
    m_rx->insertPlainText(text);
    if (m_rx->document()->characterCount() > maxRxChars)
    {
        QTextCursor c(m_rx->document());
        c.movePosition(QTextCursor::Start);
        c.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, 200);
        c.removeSelectedText();
    }
    if (atEnd)
        sb->setValue(sb->maximum());
}

void RttyPanel::sendTyped()
{
    const QString t = m_txText->toPlainText();
    if (t.trimmed().isEmpty())
        return;
    m_rtty->send(t.endsWith(QLatin1Char('\n')) ? t : t + QLatin1Char(' '));
    if (m_rtty->transmitting())
        m_txText->clear();
}

bool RttyPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_txText && event->type() == QEvent::KeyPress)
    {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && (ke->modifiers() & Qt::ControlModifier))
        {
            sendTyped();
            return true;
        }
    }
    if (obj == m_rx->viewport() && event->type() == QEvent::MouseButtonDblClick)
    {
        // Let the text edit select the word first, then pick it up.
        const bool handled = QWidget::eventFilter(obj, event);
        QMetaObject::invokeMethod(
            this,
            [this]()
            {
                const QString word = m_rx->textCursor().selectedText().trimmed().toUpper();
                static const QRegularExpression callRe(QStringLiteral("^[A-Z0-9/]{3,12}$"));
                if (callRe.match(word).hasMatch() && word.contains(QRegularExpression(QStringLiteral("[0-9]")))
                    && word.contains(QRegularExpression(QStringLiteral("[A-Z]"))))
                {
                    m_rtty->setCall(word);
                    m_call->setText(word);
                }
            },
            Qt::QueuedConnection);
        return handled;
    }
    return QWidget::eventFilter(obj, event);
}

void RttyPanel::editMacros()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("RTTY macros"));
    auto *outer = new QVBoxLayout(&dlg);
    outer->addWidget(new QLabel(tr("{MYCALL} and {CALL} are replaced when the macro is sent. End a macro with a new "
                                   "line to start the next one on a fresh line.")));
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget();
    auto *form = new QGridLayout(inner);
    QList<QLineEdit *> names;
    QList<QPlainTextEdit *> texts;
    const QStringList n = m_rtty->macroNames();
    const QStringList t = m_rtty->macroTexts();
    for (int i = 0; i < RttyEngine::kMacroCount; ++i)
    {
        auto *name = new QLineEdit(n.value(i));
        name->setMaximumWidth(120);
        auto *text = new QPlainTextEdit(t.value(i));
        text->setFont(monoFont());
        text->setFixedHeight(QFontMetrics(monoFont()).lineSpacing() * 3 + 12);
        form->addWidget(new QLabel(QStringLiteral("F%1").arg(i + 1)), i, 0, Qt::AlignTop);
        form->addWidget(name, i, 1, Qt::AlignTop);
        form->addWidget(text, i, 2);
        names << name;
        texts << text;
    }
    form->setColumnStretch(2, 1);
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outer->addWidget(bb);
    dlg.resize(640, 520);
    if (dlg.exec() != QDialog::Accepted)
        return;
    for (int i = 0; i < RttyEngine::kMacroCount; ++i)
        m_rtty->setMacro(i, names[i]->text(), texts[i]->toPlainText());
}
