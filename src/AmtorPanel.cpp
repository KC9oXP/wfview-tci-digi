#include "AmtorPanel.h"

#include "AmtorEngine.h"
#include "RxText.h"
#include "UiStyle.h"
#include "WaterfallWidget.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QVBoxLayout>

AmtorPanel::AmtorPanel(AmtorEngine *engine, QSettings *settings, QWidget *parent)
    : ModemPanel(engine, settings, QStringLiteral("AMTOR"), parent)
    , m_am(engine)
{
    QWidget *top = buildTop();
    m_rx = new QPlainTextEdit();
    RxScrollback::attach(m_rx, QStringLiteral("AMTOR"));
    m_rx->setReadOnly(true);
    QFont f(QStringLiteral("Consolas"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(f.pointSizeF() * 1.15);
    m_rx->setFont(f);
    m_rx->setMinimumHeight(60);
    m_rx->setToolTip(tr("Double-click a call sign to copy it to the Call field."));
    m_rx->viewport()->installEventFilter(this);

    connect(m_am, &ModemEngine::statsChanged, this, &AmtorPanel::syncStats);
    connect(m_am, &ModemEngine::textAppended, this, [this](const QString &t) { RxText::append(m_rx, t, 1000000); });
    connect(waterfall(), &WaterfallWidget::clicked, m_am, &ModemEngine::setCarrierHz);
    finishLayout(top, m_rx);
    syncStats();
}

QWidget *AmtorPanel::buildTop()
{
    auto *top = new QWidget();
    auto *v = new QVBoxLayout(top);
    v->setContentsMargins(0, 0, 0, 0);

    auto *r1 = new QHBoxLayout();
    m_decode = new QPushButton(tr("Decode"));
    m_decode->setToolTip(tr("Receive AMTOR FEC / SITOR-B (100 baud, 170 Hz shift)."));
    UiStyle::decodeButton(m_decode);
    r1->addWidget(m_decode);
    r1->addWidget(new QLabel(tr("Centre")));
    m_freq = new QDoubleSpinBox();
    m_freq->setRange(ModemEngine::kMinHz + 150.0, ModemEngine::kMaxHz - 150.0);
    m_freq->setDecimals(0);
    m_freq->setSuffix(tr(" Hz"));
    m_freq->setKeyboardTracking(false);
    m_freq->setToolTip(tr("Centre between the two tones (mark = centre + 85 Hz in USB). Click the waterfall "
                          "to tune; receive and transmit use the same frequency."));
    r1->addWidget(m_freq);
    m_reverse = new QCheckBox(tr("Reverse"));
    m_reverse->setToolTip(tr("Swap the tones (for LSB, or a station sending inverted). Applies to receive and "
                             "transmit."));
    r1->addWidget(m_reverse);
    r1->addStretch(1);
    auto *clear = new QPushButton(tr("Clear"));
    r1->addWidget(clear);
    v->addLayout(r1);

    m_stats = new QLabel();
    v->addWidget(m_stats);
    v->addWidget(waterfall(), 1);
    v->addWidget(statusLabel());

    connect(m_decode, &QPushButton::toggled, m_am, &ModemEngine::setDecoding);
    connect(m_freq, &QDoubleSpinBox::valueChanged, m_am, &ModemEngine::setCarrierHz);
    connect(m_reverse, &QCheckBox::toggled, m_am, &AmtorEngine::setReverse);
    connect(clear, &QPushButton::clicked, this, [this]() { m_rx->clear(); });
    return top;
}

void AmtorPanel::syncSettings()
{
    ModemPanel::syncSettings();
    const QSignalBlocker b1(m_decode), b2(m_freq), b3(m_reverse);
    m_decode->setChecked(m_am->decoding());
    if (!m_freq->hasFocus())
        m_freq->setValue(m_am->carrierHz());
    m_reverse->setChecked(m_am->reverse());
    syncStats();
}

void AmtorPanel::syncStats()
{
    QString s = tr("AMTOR FEC (mode B): 100 baud, 170 Hz shift, every character sent twice. "
                   "Transmit starts with about 2.5 s of phasing signals.");
    if (m_am->decoding())
        s = tr("%1   level %2 dB")
                .arg(m_am->locked() ? tr("<b>Locked</b>") : tr("Searching"))
                .arg(qRound(m_am->levelDb()));
    m_stats->setText(s);
}

bool AmtorPanel::eventFilter(QObject *obj, QEvent *event)
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
