#include "PskPanel.h"

#include "ModemTxBox.h"
#include "RxText.h"
#include "SquelchControl.h"
#include "UiStyle.h"
#include "WaterfallWidget.h"
#include "callspot.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTextCursor>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int maxRxChars = 1000000;

// The word at character index i of text (letters, digits and '/').
QString wordAt(const QString &text, int i)
{
    if (i < 0 || i >= text.size())
        return QString();
    const auto part = [](QChar ch) { return ch.isLetterOrNumber() || ch == QLatin1Char('/'); };
    int a = i, b = i;
    while (a > 0 && part(text.at(a - 1)))
        --a;
    while (b < text.size() && part(text.at(b)))
        ++b;
    return text.mid(a, b - a);
}

// Call signs in a row's text, newest first, each once.
QStringList callsIn(const QString &text)
{
    QStringList out;
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (auto it = words.crbegin(); it != words.crend() && out.size() < 6; ++it)
    {
        const QString w = it->toUpper();
        if (!out.contains(w) && callspot::isCallsign(w.toStdString()))
            out << w;
    }
    return out;
}
} // namespace

PskPanel::PskPanel(PskEngine *engine, QSettings *settings, QWidget *parent)
    : ModemPanel(engine, settings, QStringLiteral("PSK"), parent)
    , m_psk(engine)
{
    QWidget *top = buildTop();
    m_rx = new QPlainTextEdit();
    RxScrollback::attach(m_rx, QStringLiteral("PSK"));
    m_rx->setReadOnly(true);
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    m_rx->setFont(f);
    m_rx->setMinimumHeight(60);
    m_rx->setToolTip(tr("Double-click a call sign to copy it to the Call field."));
    m_rx->viewport()->installEventFilter(this);

    connect(m_psk, &ModemEngine::statsChanged, this, &PskPanel::syncStats);
    connect(m_psk, &ModemEngine::textAppended, this, &PskPanel::appendText);
    connect(waterfall(), &WaterfallWidget::clicked, m_psk, &PskEngine::tuneTo);

    // Multi-signal list under the receive pane.
    m_table = new QTableWidget(0, 3);
    m_table->setObjectName(QStringLiteral("pskMultiTable"));
    m_table->setHorizontalHeaderLabels({tr("Freq"), tr("SNR"), tr("Text")});
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFont(f);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->viewport()->installEventFilter(this);
    m_table->setToolTip(tr("Every PSK signal at this speed in 200-3000 Hz, each on its own row.\nClick a row to "
                           "decode that station above (what the row copied so far is put there first). "
                           "Double-click a call sign in the Text column to log it; right-click for both."));
    m_rxSplit = new QSplitter(Qt::Vertical);
    m_rxSplit->setChildrenCollapsible(false);
    m_rxSplit->addWidget(m_rx);
    m_rxSplit->addWidget(m_table);
    m_rxSplit->setStretchFactor(0, 3);
    m_rxSplit->setStretchFactor(1, 2);
    if (settings)
        m_rxSplit->restoreState(settings->value(QStringLiteral("PSK/MultiSplit")).toByteArray());

    connect(m_psk, &PskEngine::channelsChanged, this, &PskPanel::showChannels);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) { decodeRow(row); });
    connect(m_table,
            &QWidget::customContextMenuRequested,
            this,
            [this](const QPoint &pos)
            {
                const int row = m_table->rowAt(pos.y());
                auto *freq = m_table->item(row, 0);
                auto *text = m_table->item(row, 2);
                if (!freq)
                    return;
                QMenu menu(this);
                const double hz = freq->data(Qt::UserRole).toDouble();
                const QString history = text ? text->data(Qt::UserRole).toString() : QString();
                menu.addAction(tr("Decode this station above (%1 Hz)").arg(qRound(hz)),
                               this,
                               [this, hz, history]() { m_psk->decodeChannel(hz, history); });
                const QStringList calls = callsIn(history);
                if (!calls.isEmpty())
                    menu.addSeparator();
                for (const QString &c : calls)
                    menu.addAction(tr("Log %1 (QSO log)").arg(c),
                                   this,
                                   [this, c, hz, history]()
                                   {
                                       m_psk->decodeChannel(hz, history);
                                       pickCall(c);
                                   });
                menu.exec(m_table->viewport()->mapToGlobal(pos));
            });

    finishLayout(top, m_rxSplit);
    syncStats();
}

void PskPanel::saveState()
{
    ModemPanel::saveState();
    if (m_settings && m_rxSplit)
        m_settings->setValue(QStringLiteral("PSK/MultiSplit"), m_rxSplit->saveState());
}

void PskPanel::decodeRow(int row)
{
    auto *freq = m_table->item(row, 0);
    auto *text = m_table->item(row, 2);
    if (freq)
        m_psk->decodeChannel(freq->data(Qt::UserRole).toDouble(),
                             text ? text->data(Qt::UserRole).toString() : QString());
}

void PskPanel::pickCall(const QString &call)
{
    emit RxCalls::instance() -> callPicked(call, QStringLiteral("PSK")); // QSO log: call + lookup
    txBox()->offerCall(call);
}

void PskPanel::showChannels(const QVector<PskChannelInfo> &channels)
{
    m_table->setRowCount(channels.size());
    const QColor dim = palette().color(QPalette::Disabled, QPalette::Text);
    const QColor normal = palette().color(QPalette::Text);
    for (int r = 0; r < channels.size(); ++r)
    {
        const PskChannelInfo &c = channels[r];
        const QStringList cells
            = {QString::number(qRound(c.hz)), QString::number(c.snrDb, 'f', 0), c.text.right(160)};
        for (int col = 0; col < cells.size(); ++col)
        {
            auto *it = m_table->item(r, col);
            if (!it)
            {
                it = new QTableWidgetItem();
                m_table->setItem(r, col, it);
            }
            it->setText(cells[col]);
            it->setForeground(c.active ? normal : dim);
            if (col == 0)
                it->setData(Qt::UserRole, c.hz);
            if (col == 2)
                it->setData(Qt::UserRole, c.text); // all of it, for a pick and the right-click call list
        }
    }
}

QWidget *PskPanel::buildTop()
{
    auto *top = new QWidget();
    auto *v = new QVBoxLayout(top);
    v->setContentsMargins(0, 0, 0, 0);

    auto *r1 = new QHBoxLayout();
    m_decode = new UiStyle::DecodeButton();
    UiStyle::decodeButton(m_decode);
    r1->addWidget(m_decode);
    m_speed = new QComboBox();
    for (int s = psk::Bpsk31; s <= psk::Bpsk125; ++s)
        m_speed->addItem(QString::fromLatin1(psk::speedName(s)), s);
    r1->addWidget(m_speed);
    m_autoSpeed = new QCheckBox(tr("Auto"));
    m_autoSpeed->setToolTip(tr("When you click a signal on the waterfall, also set BPSK31 / 63 / 125 from the "
                               "last 5 seconds of it."));
    r1->addWidget(m_autoSpeed);
    auto *detect = new QPushButton(tr("Detect"));
    detect->setToolTip(tr("Set BPSK31 / 63 / 125 from the last 5 seconds of audio at the receive frequency "
                          "(works on idle signals too)."));
    r1->addWidget(detect);
    r1->addWidget(new QLabel(tr("Freq")));
    m_freq = new QDoubleSpinBox();
    m_freq->setRange(ModemEngine::kMinHz, ModemEngine::kMaxHz);
    m_freq->setDecimals(1);
    m_freq->setSingleStep(1.0);
    m_freq->setSuffix(tr(" Hz"));
    m_freq->setKeyboardTracking(false);
    m_freq->setToolTip(tr("Audio frequency of the signal (receive and transmit). Click the waterfall to tune."));
    r1->addWidget(m_freq);
    m_afc = new QCheckBox(tr("AFC"));
    m_afc->setToolTip(tr("Follow the signal's frequency (within one baud of where you tuned)."));
    r1->addWidget(m_afc);
    {
        SquelchControl::Scale q, db;
        q.max = 0.95;
        q.tip = tr("Phase-quality squelch: noise reads about 0.1-0.35, a good signal near 1. Default 0.30.");
        db.max = 30.0;
        db.step = 0.5;
        db.decimals = 1;
        db.unit = tr(" dB");
        db.tip = tr("S/N squelch: signal-to-noise in the PSK symbol bandwidth, from the phase quality (noise "
                    "alone reads below 3 dB, a clean signal 20-40 dB). Default 3 dB.");
        m_squelch = new SquelchControl(q, db, false);
        r1->addWidget(m_squelch, 1);
    }
    auto *standard = new QPushButton(tr("Standard"));
    standard->setToolTip(tr("Back to the usual settings: BPSK31, AFC on, squelch 0.30 (the frequency is not "
                            "changed)."));
    r1->addWidget(standard);
    connect(standard,
            &QPushButton::clicked,
            this,
            [this]()
            {
                m_psk->setSpeed(psk::Bpsk31);
                m_psk->setAfc(true);
                m_psk->setSquelchInDb(false);
                m_psk->setSquelch(0.3);
            });
    auto *clear = new QPushButton(tr("Clear"));
    r1->addWidget(clear);
    v->addLayout(r1);

    auto *r2 = new QHBoxLayout();
    m_multi = new QCheckBox(tr("Multi-signal"));
    m_multi->setObjectName(QStringLiteral("pskMulti"));
    m_multi->setToolTip(tr("List every PSK signal at this speed in the passband (200-3000 Hz), each copied on its "
                           "own row.\nClick a row to decode that station above: what the row copied so far is put "
                           "there first. Rows use the squelch setting, but never looser than the defaults."));
    r2->addWidget(m_multi);
    r2->addWidget(new QLabel(tr("Quality")));
    m_qualityBar = new QProgressBar();
    m_qualityBar->setRange(0, 100);
    m_qualityBar->setTextVisible(false);
    m_qualityBar->setMaximumHeight(12);
    r2->addWidget(m_qualityBar, 1);
    m_level = new QLabel();
    m_level->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("Level -100 dB")) + 4);
    r2->addWidget(m_level);
    v->addLayout(r2);

    v->addWidget(waterfall(), 1);
    v->addWidget(statusLabel());

    connect(m_decode, &QPushButton::toggled, m_psk, &ModemEngine::setDecoding);
    connect(m_speed, &QComboBox::activated, this, [this](int i) { m_psk->setSpeed(m_speed->itemData(i).toInt()); });
    connect(m_freq, &QDoubleSpinBox::valueChanged, m_psk, &ModemEngine::setCarrierHz);
    connect(m_afc, &QCheckBox::toggled, m_psk, &PskEngine::setAfc);
    connect(m_autoSpeed, &QCheckBox::toggled, m_psk, &PskEngine::setAutoSpeed);
    connect(m_multi, &QCheckBox::toggled, m_psk, &PskEngine::setMultiSignal);
    connect(detect, &QPushButton::clicked, m_psk, &PskEngine::detectSpeed);
    connect(m_squelch, &SquelchControl::valueChanged, m_psk, &PskEngine::setSquelch);
    connect(m_squelch, &SquelchControl::inDbToggled, m_psk, &PskEngine::setSquelchInDb);
    connect(clear, &QPushButton::clicked, this, [this]() { m_rx->clear(); });
    return top;
}

double PskPanel::markerBandwidth() const
{
    return m_psk->baud();
}

void PskPanel::syncSettings()
{
    ModemPanel::syncSettings();
    const QSignalBlocker b1(m_decode), b2(m_speed), b3(m_freq), b4(m_afc), b6(m_autoSpeed), b7(m_multi);
    m_multi->setChecked(m_psk->multiSignal());
    m_table->setVisible(m_psk->multiSignal());
    m_autoSpeed->setChecked(m_psk->autoSpeed());
    m_decode->setChecked(m_psk->decoding());
    m_speed->setCurrentIndex(m_speed->findData(m_psk->speed()));
    if (!m_freq->hasFocus())
        m_freq->setValue(m_psk->carrierHz());
    m_afc->setChecked(m_psk->afc());
    m_squelch->setSetting(m_psk->squelch(), m_psk->squelchInDb());
}

void PskPanel::syncStats()
{
    m_qualityBar->setValue(m_psk->decoding() ? std::clamp(int(std::lround(m_psk->quality() * 100)), 0, 100) : 0);
    m_qualityBar->setToolTip(tr("Quality %1 (squelch %2)")
                                 .arg(m_psk->quality(), 0, 'f', 2)
                                 .arg(m_psk->squelchOpen() ? tr("open") : tr("closed")));
    m_level->setText(m_psk->decoding() ? tr("Level %1 dB").arg(qRound(m_psk->levelDb())) : QString());
    m_squelch->setReading(m_psk->squelchLevel(), m_psk->squelchOpen(), m_psk->decoding());
}

void PskPanel::appendText(const QString &text)
{
    RxText::append(m_rx, text, maxRxChars);
}

void PskPanel::addNote(const QString &text)
{
    RxText::appendNote(m_rx, text);
}

bool PskPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (m_table && obj == m_table->viewport() && event->type() == QEvent::MouseButtonDblClick)
    {
        const QPoint pos = static_cast<QMouseEvent *>(event)->position().toPoint();
        const QModelIndex idx = m_table->indexAt(pos);
        if (!idx.isValid())
            return true;
        if (idx.column() == 2)
        {
            // The word under the mouse (fixed-width font, drawn from the left after a small margin).
            const QString shown = m_table->item(idx.row(), 2) ? m_table->item(idx.row(), 2)->text() : QString();
            const QRect r = m_table->visualRect(idx);
            const int margin = m_table->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, m_table) + 1;
            const int cw = std::max(1, m_table->fontMetrics().horizontalAdvance(QLatin1Char('M')));
            const QString w = wordAt(shown, (pos.x() - r.left() - margin) / cw).toUpper();
            decodeRow(idx.row()); // the station whose call is picked goes above too
            if (callspot::isCallsign(w.toStdString()))
                pickCall(w);
            return true;
        }
        decodeRow(idx.row());
        return true;
    }
    if (m_rx && obj == m_rx->viewport() && event->type() == QEvent::MouseButtonDblClick)
    {
        const bool handled = ModemPanel::eventFilter(obj, event);
        QMetaObject::invokeMethod(
            this, [this]() { setCallFromReceive(m_rx->textCursor().selectedText()); }, Qt::QueuedConnection);
        return handled;
    }
    return ModemPanel::eventFilter(obj, event);
}
