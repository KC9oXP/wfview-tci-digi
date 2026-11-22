#include "Mt63Panel.h"

#include "Mt63Engine.h"
#include "RxText.h"
#include "UiStyle.h"
#include "WaterfallWidget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTextCursor>
#include <QVBoxLayout>
#include <cmath>

namespace
{
// Combo item data: bandwidth, + 1 for long interleave.
int packMode(int bandwidth, bool longInterleave)
{
    return bandwidth + (longInterleave ? 1 : 0);
}
} // namespace

Mt63Panel::Mt63Panel(Mt63Engine *engine, QSettings *settings, QWidget *parent)
    : ModemPanel(engine, settings, QStringLiteral("MT63"), parent)
    , m_mt(engine)
{
    QWidget *top = buildTop();
    m_rx = new QPlainTextEdit();
    RxScrollback::attach(m_rx, QStringLiteral("MT63"));
    m_rx->setReadOnly(true);
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    m_rx->setFont(f);
    m_rx->setMinimumHeight(60);
    m_rx->setToolTip(tr("Double-click a call sign to copy it to the Call field."));
    m_rx->viewport()->installEventFilter(this);

    connect(m_mt, &ModemEngine::statsChanged, this, &Mt63Panel::syncStats);
    connect(m_mt, &ModemEngine::textAppended, this, [this](const QString &t) { RxText::append(m_rx, t, 1000000); });
    connect(waterfall(), &WaterfallWidget::clicked, m_mt, &ModemEngine::setCarrierHz);
    finishLayout(top, m_rx);
    syncStats();
}

QWidget *Mt63Panel::buildTop()
{
    auto *top = new QWidget();
    auto *v = new QVBoxLayout(top);
    v->setContentsMargins(0, 0, 0, 0);

    auto *r1 = new QHBoxLayout();
    m_decode = new UiStyle::DecodeButton();
    UiStyle::decodeButton(m_decode);
    r1->addWidget(m_decode);
    m_mode = new QComboBox();
    for (int bw : {500, 1000, 2000})
        for (bool lg : {false, true})
            m_mode->addItem(QString::fromStdString(mt63::name({bw, lg, 1500.0, false})), packMode(bw, lg));
    m_mode->setToolTip(tr("Bandwidth (500 / 1000 / 2000 Hz) and interleave: S short, L long (steadier through "
                          "fades, twice the delay). MT63-1000L is the usual one on HF nets."));
    r1->addWidget(m_mode);
    r1->addWidget(new QLabel(tr("Centre")));
    m_freq = new QDoubleSpinBox();
    m_freq->setRange(ModemEngine::kMinHz, ModemEngine::kMaxHz);
    m_freq->setDecimals(0);
    m_freq->setSuffix(tr(" Hz"));
    m_freq->setKeyboardTracking(false);
    m_freq->setToolTip(tr("Centre of the signal (receive and transmit). Click the waterfall to tune.\n"
                          "The receiver follows a few tens of Hz of offset by itself."));
    r1->addWidget(m_freq);
    r1->addWidget(new QLabel(tr("Squelch")));
    m_squelch = new QSlider(Qt::Horizontal);
    m_squelch->setRange(0, 200);
    m_squelch->setMinimumWidth(70);
    m_squelch->setToolTip(tr("Minimum S/N (dB, as the decoder sees it) to print. Noise alone reads up to about 5; "
                             "0 prints everything, noise included (random characters)."));
    r1->addWidget(m_squelch, 1);
    m_squelchValue = new QLabel();
    m_squelchValue->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("20.0")) + 4);
    r1->addWidget(m_squelchValue);
    auto *standard = new QPushButton(tr("Standard"));
    standard->setToolTip(tr("Back to the usual setting: MT63-1000L, squelch 5.5 (the frequency is not changed)."));
    r1->addWidget(standard);
    connect(standard,
            &QPushButton::clicked,
            this,
            [this]()
            {
                m_mt->setMode(1000, true);
                m_mt->setSquelch(5.5);
            });
    auto *clear = new QPushButton(tr("Clear"));
    r1->addWidget(clear);
    v->addLayout(r1);

    m_stats = new QLabel();
    v->addWidget(m_stats);
    v->addWidget(waterfall(), 1);
    v->addWidget(statusLabel());

    connect(m_decode, &QPushButton::toggled, m_mt, &ModemEngine::setDecoding);
    connect(m_mode,
            &QComboBox::activated,
            this,
            [this](int i)
            {
                const int v = m_mode->itemData(i).toInt();
                m_mt->setMode(v & ~1, (v & 1) != 0);
            });
    connect(m_freq, &QDoubleSpinBox::valueChanged, m_mt, &ModemEngine::setCarrierHz);
    connect(m_squelch, &QSlider::valueChanged, this, [this](int val) { m_mt->setSquelch(val / 10.0); });
    connect(clear, &QPushButton::clicked, this, [this]() { m_rx->clear(); });
    return top;
}

double Mt63Panel::markerBandwidth() const
{
    return m_mt->bandwidth();
}

void Mt63Panel::syncSettings()
{
    ModemPanel::syncSettings();
    const QSignalBlocker b1(m_decode), b2(m_mode), b3(m_freq), b4(m_squelch);
    m_decode->setChecked(m_mt->decoding());
    m_mode->setCurrentIndex(m_mode->findData(packMode(m_mt->bandwidth(), m_mt->longInterleave())));
    if (!m_freq->hasFocus())
        m_freq->setValue(m_mt->carrierHz());
    m_squelch->setValue(qRound(m_mt->squelch() * 10));
    m_squelchValue->setText(QString::number(m_mt->squelch(), 'f', 1));
    syncStats();
}

void Mt63Panel::syncStats()
{
    const mt63::Config c = m_mt->config();
    QString s = tr("%1: %2 characters/s, %3 s interleave")
                    .arg(m_mt->modeName())
                    .arg(mt63::charsPerSecond(c), 0, 'f', 0)
                    .arg(mt63::delaySeconds(c), 0, 'f', 1);
    if (m_mt->decoding())
        s += tr("    S/N %1 dB   %2   offset %3 Hz")
                 .arg(m_mt->snrDb(), 0, 'f', 1)
                 .arg(m_mt->locked() ? tr("locked") : tr("searching"))
                 .arg(m_mt->offsetHz(), 0, 'f', 1);
    m_stats->setText(s);
}

bool Mt63Panel::eventFilter(QObject *obj, QEvent *event)
{
    if (m_rx && obj == m_rx->viewport() && event->type() == QEvent::MouseButtonDblClick)
    {
        const bool handled = ModemPanel::eventFilter(obj, event);
        QMetaObject::invokeMethod(
            this, [this]() { setCallFromReceive(m_rx->textCursor().selectedText()); }, Qt::QueuedConnection);
        return handled;
    }
    return ModemPanel::eventFilter(obj, event);
}

void Mt63Panel::addNote(const QString &text)
{
    RxText::appendNote(m_rx, text);
}
