#include "PskPanel.h"

#include "PskEngine.h"
#include "WaterfallWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QTextCursor>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int maxRxChars = 200000;
}

PskPanel::PskPanel(PskEngine *engine, QSettings *settings, QWidget *parent)
    : ModemPanel(engine, settings, QStringLiteral("PSK"), parent)
    , m_psk(engine)
{
    QWidget *top = buildTop();
    m_rx = new QPlainTextEdit();
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
    finishLayout(top, m_rx);
    syncStats();
}

QWidget *PskPanel::buildTop()
{
    auto *top = new QWidget();
    auto *v = new QVBoxLayout(top);
    v->setContentsMargins(0, 0, 0, 0);

    auto *r1 = new QHBoxLayout();
    m_decode = new QPushButton(tr("Decode"));
    m_decode->setCheckable(true);
    r1->addWidget(m_decode);
    m_speed = new QComboBox();
    for (int s = psk::Bpsk31; s <= psk::Bpsk125; ++s)
        m_speed->addItem(QString::fromLatin1(psk::speedName(s)), s);
    r1->addWidget(m_speed);
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
    r1->addWidget(new QLabel(tr("Squelch")));
    m_squelch = new QSlider(Qt::Horizontal);
    m_squelch->setRange(0, 95);
    m_squelch->setMinimumWidth(70);
    m_squelch->setToolTip(tr("Phase-quality squelch: noise reads about 0.1-0.3, a good signal near 1."));
    r1->addWidget(m_squelch, 1);
    m_squelchValue = new QLabel();
    m_squelchValue->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("0.00")) + 4);
    r1->addWidget(m_squelchValue);
    auto *clear = new QPushButton(tr("Clear"));
    r1->addWidget(clear);
    v->addLayout(r1);

    auto *r2 = new QHBoxLayout();
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
    connect(m_squelch, &QSlider::valueChanged, this, [this](int val) { m_psk->setSquelch(val / 100.0); });
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
    const QSignalBlocker b1(m_decode), b2(m_speed), b3(m_freq), b4(m_afc), b5(m_squelch);
    m_decode->setChecked(m_psk->decoding());
    m_speed->setCurrentIndex(m_speed->findData(m_psk->speed()));
    if (!m_freq->hasFocus())
        m_freq->setValue(m_psk->carrierHz());
    m_afc->setChecked(m_psk->afc());
    m_squelch->setValue(qRound(m_psk->squelch() * 100));
    m_squelchValue->setText(QString::number(m_psk->squelch(), 'f', 2));
}

void PskPanel::syncStats()
{
    m_qualityBar->setValue(m_psk->decoding() ? std::clamp(int(std::lround(m_psk->quality() * 100)), 0, 100) : 0);
    m_qualityBar->setToolTip(tr("Quality %1 (squelch %2)")
                                 .arg(m_psk->quality(), 0, 'f', 2)
                                 .arg(m_psk->squelchOpen() ? tr("open") : tr("closed")));
    m_level->setText(m_psk->decoding() ? tr("Level %1 dB").arg(qRound(m_psk->levelDb())) : QString());
}

void PskPanel::appendText(const QString &text)
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

bool PskPanel::eventFilter(QObject *obj, QEvent *event)
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
