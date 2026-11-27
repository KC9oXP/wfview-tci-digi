#include "OliviaPanel.h"

#include "OliviaEngine.h"
#include "RxText.h"
#include "SquelchControl.h"
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
int packMode(bool contestia, int tones, int bandwidth)
{
    return (contestia ? 1 << 24 : 0) | (tones << 12) | bandwidth;
}
} // namespace

OliviaPanel::OliviaPanel(OliviaEngine *engine, QSettings *settings, QWidget *parent)
    : ModemPanel(engine, settings, QStringLiteral("Olivia"), parent)
    , m_ol(engine)
{
    QWidget *top = buildTop();
    m_rx = new QPlainTextEdit();
    RxScrollback::attach(m_rx, QStringLiteral("Olivia"));
    m_rx->setReadOnly(true);
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    m_rx->setFont(f);
    m_rx->setMinimumHeight(60);
    m_rx->setToolTip(tr("Double-click a call sign to copy it to the Call field."));
    m_rx->viewport()->installEventFilter(this);

    connect(m_ol, &ModemEngine::statsChanged, this, &OliviaPanel::syncStats);
    connect(m_ol, &ModemEngine::textAppended, this, [this](const QString &t) { RxText::append(m_rx, t, 1000000); });
    connect(waterfall(), &WaterfallWidget::clicked, m_ol, &ModemEngine::setCarrierHz);
    finishLayout(top, m_rx);
    syncStats();
}

QWidget *OliviaPanel::buildTop()
{
    auto *top = new QWidget();
    auto *v = new QVBoxLayout(top);
    v->setContentsMargins(0, 0, 0, 0);

    auto *r1 = new QHBoxLayout();
    m_decode = new UiStyle::DecodeButton();
    UiStyle::decodeButton(m_decode);
    r1->addWidget(m_decode);
    m_mode = new QComboBox();
    for (const OliviaEngine::Preset &p : OliviaEngine::presets())
        m_mode->addItem(OliviaEngine::presetName(p), packMode(p.contestia, p.tones, p.bandwidth));
    m_mode->setToolTip(tr("Olivia or Contestia, tones / bandwidth. Both stations must use the same."));
    r1->addWidget(m_mode);
    r1->addWidget(new QLabel(tr("Centre")));
    m_freq = new QDoubleSpinBox();
    m_freq->setRange(ModemEngine::kMinHz, ModemEngine::kMaxHz);
    m_freq->setDecimals(0);
    m_freq->setSuffix(tr(" Hz"));
    m_freq->setKeyboardTracking(false);
    m_freq->setToolTip(tr("Centre of the signal (receive and transmit). Click the waterfall to tune.\n"
                          "The receiver also finds signals a few tone spacings off."));
    r1->addWidget(m_freq);
    {
        SquelchControl::Scale n, db;
        n.max = 20.0;
        n.step = 0.1;
        n.decimals = 1;
        n.tip = tr("Minimum S/N of each decoded block (the decoder's own S/N for that block, so printing stops "
                   "as soon as the text ends). Noise reads up to about 3, other signals in the passband (an RTTY "
                   "pair) about 5, a good signal 8-12. Default 6.0.\n"
                   "It opens on two good blocks in a row once the average S/N has got there too; up to four weak "
                   "blocks in a fade are held and printed when a good one follows.");
        db.max = 30.0;
        db.step = 0.5;
        db.decimals = 1;
        db.unit = tr(" dB");
        db.tip = tr("The same block S/N in dB (20 log10): noise reads up to about 10 dB, a good signal 18-22 dB. "
                    "Default 15.5 dB (about 6.0).");
        m_squelch = new SquelchControl(n, db, false);
        r1->addWidget(m_squelch, 1);
    }
    auto *standard = new QPushButton(tr("Standard"));
    standard->setToolTip(tr("Back to the most used setting: Olivia 8 tones / 500 Hz, squelch 6.0 (the frequency "
                            "is not changed)."));
    r1->addWidget(standard);
    connect(standard,
            &QPushButton::clicked,
            this,
            [this]()
            {
                m_ol->setMode(false, 8, 500);
                m_ol->setSquelchInDb(false);
                m_ol->setSquelch(6.0);
            });
    auto *clear = new QPushButton(tr("Clear"));
    r1->addWidget(clear);
    v->addLayout(r1);

    m_stats = new QLabel();
    v->addWidget(m_stats);
    v->addWidget(waterfall(), 1);
    v->addWidget(statusLabel());

    connect(m_decode, &QPushButton::toggled, m_ol, &ModemEngine::setDecoding);
    connect(m_mode,
            &QComboBox::activated,
            this,
            [this](int i)
            {
                // Item data, not the preset list: a mode set by RSID adds its own item.
                const int v = m_mode->itemData(i).toInt();
                m_ol->setMode((v >> 24) != 0, (v >> 12) & 0xfff, v & 0xfff);
            });
    connect(m_freq, &QDoubleSpinBox::valueChanged, m_ol, &ModemEngine::setCarrierHz);
    connect(m_squelch, &SquelchControl::valueChanged, m_ol, &OliviaEngine::setSquelch);
    connect(m_squelch, &SquelchControl::inDbToggled, m_ol, &OliviaEngine::setSquelchInDb);
    connect(clear, &QPushButton::clicked, this, [this]() { m_rx->clear(); });
    return top;
}

double OliviaPanel::markerBandwidth() const
{
    return m_ol->bandwidth();
}

void OliviaPanel::syncSettings()
{
    ModemPanel::syncSettings();
    const QSignalBlocker b1(m_decode), b2(m_mode), b3(m_freq);
    m_decode->setChecked(m_ol->decoding());
    // A mode outside the list (set by RSID, e.g. Olivia 8/1000) gets an item of its own.
    if (m_mode->findText(m_ol->modeName()) < 0)
        m_mode->addItem(m_ol->modeName(), packMode(m_ol->contestia(), m_ol->tones(), m_ol->bandwidth()));
    m_mode->setCurrentText(m_ol->modeName());
    if (!m_freq->hasFocus())
        m_freq->setValue(m_ol->carrierHz());
    m_squelch->setSetting(m_ol->squelch(), m_ol->squelchInDb());
    syncStats();
}

void OliviaPanel::syncStats()
{
    const double cps = olivia::charsPerSecond(m_ol->config());
    QString s = tr("%1: %2 characters/s").arg(m_ol->modeName()).arg(cps, 0, 'f', 2);
    if (m_ol->decoding())
        s += tr("    S/N %1   offset %2 Hz").arg(m_ol->snr(), 0, 'f', 1).arg(qRound(m_ol->offsetHz()));
    m_stats->setText(s);
    m_squelch->setReading(m_ol->squelchLevel(), m_ol->squelchOpen(), m_ol->decoding());
}

bool OliviaPanel::eventFilter(QObject *obj, QEvent *event)
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

void OliviaPanel::addNote(const QString &text)
{
    RxText::appendNote(m_rx, text);
}
