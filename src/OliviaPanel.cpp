#include "OliviaPanel.h"

#include "OliviaEngine.h"
#include "RxText.h"
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
    m_decode = new QPushButton(tr("Decode"));
    m_decode->setCheckable(true);
    r1->addWidget(m_decode);
    m_mode = new QComboBox();
    for (const OliviaEngine::Preset &p : OliviaEngine::presets())
        m_mode->addItem(OliviaEngine::presetName(p));
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
    r1->addWidget(new QLabel(tr("Squelch")));
    m_squelch = new QSlider(Qt::Horizontal);
    m_squelch->setRange(0, 200);
    m_squelch->setMinimumWidth(70);
    m_squelch->setToolTip(tr("Minimum S/N to print. 0 prints everything, noise included (random characters)."));
    r1->addWidget(m_squelch, 1);
    m_squelchValue = new QLabel();
    m_squelchValue->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("20.0")) + 4);
    r1->addWidget(m_squelchValue);
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
                const OliviaEngine::Preset p = OliviaEngine::presets().at(i);
                m_ol->setMode(p.contestia, p.tones, p.bandwidth);
            });
    connect(m_freq, &QDoubleSpinBox::valueChanged, m_ol, &ModemEngine::setCarrierHz);
    connect(m_squelch, &QSlider::valueChanged, this, [this](int val) { m_ol->setSquelch(val / 10.0); });
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
    const QSignalBlocker b1(m_decode), b2(m_mode), b3(m_freq), b4(m_squelch);
    m_decode->setChecked(m_ol->decoding());
    m_mode->setCurrentText(m_ol->modeName());
    if (!m_freq->hasFocus())
        m_freq->setValue(m_ol->carrierHz());
    m_squelch->setValue(qRound(m_ol->squelch() * 10));
    m_squelchValue->setText(QString::number(m_ol->squelch(), 'f', 1));
    syncStats();
}

void OliviaPanel::syncStats()
{
    const double cps = olivia::charsPerSecond(m_ol->config());
    QString s = tr("%1: %2 characters/s").arg(m_ol->modeName()).arg(cps, 0, 'f', 2);
    if (m_ol->decoding())
        s += tr("    S/N %1   offset %2 Hz").arg(m_ol->snr(), 0, 'f', 1).arg(qRound(m_ol->offsetHz()));
    m_stats->setText(s);
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
