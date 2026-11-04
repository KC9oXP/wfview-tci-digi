#include "HellPanel.h"

#include "HellEngine.h"
#include "HellWidget.h"
#include "WaterfallWidget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTextCursor>
#include <QVBoxLayout>

HellPanel::HellPanel(HellEngine *engine, QSettings *settings, QWidget *parent)
    : ModemPanel(engine, settings, QStringLiteral("Hell"), parent)
    , m_hell(engine)
{
    QWidget *top = buildTop();

    // Receive picture, with a one-line strip below for what we transmitted.
    auto *rxBox = new QWidget();
    auto *rv = new QVBoxLayout(rxBox);
    rv->setContentsMargins(0, 0, 0, 0);
    m_rx = new HellWidget();
    m_rx->setToolTip(tr("Received Feld Hell, each column drawn twice so drifting text stays readable. "
                        "Slanted text: adjust RX trim."));
    rv->addWidget(m_rx, 1);
    m_echo = new QPlainTextEdit();
    m_echo->setReadOnly(true);
    m_echo->setMaximumBlockCount(200);
    m_echo->setFixedHeight(QFontMetrics(font()).lineSpacing() * 2 + 10);
    m_echo->setPlaceholderText(tr("Transmitted text appears here."));
    rv->addWidget(m_echo);

    if (m_settings)
    {
        m_rx->setZoom(m_settings->value(QStringLiteral("Hell/Zoom"), 2).toInt());
        m_rx->setGain(m_settings->value(QStringLiteral("Hell/Contrast"), 1.5).toDouble());
    }
    m_zoom->setCurrentIndex(m_zoom->findData(m_rx->zoom()));
    m_contrast->setValue(qRound(m_rx->gain() * 10));

    connect(m_hell, &HellEngine::columns, m_rx, &HellWidget::addColumns);
    connect(m_hell,
            &ModemEngine::textAppended,
            this,
            [this](const QString &t)
            {
                m_echo->moveCursor(QTextCursor::End);
                m_echo->insertPlainText(t);
                m_echo->ensureCursorVisible();
            });
    connect(m_hell,
            &ModemEngine::statsChanged,
            this,
            [this]()
            { m_level->setText(m_hell->decoding() ? tr("Level %1 dB").arg(qRound(m_hell->levelDb())) : QString()); });
    connect(waterfall(), &WaterfallWidget::clicked, m_hell, &ModemEngine::setCarrierHz);
    finishLayout(top, rxBox);
}

QWidget *HellPanel::buildTop()
{
    auto *top = new QWidget();
    auto *v = new QVBoxLayout(top);
    v->setContentsMargins(0, 0, 0, 0);

    auto *r1 = new QHBoxLayout();
    m_decode = new QPushButton(tr("Decode"));
    m_decode->setCheckable(true);
    r1->addWidget(m_decode);
    r1->addWidget(new QLabel(tr("Freq")));
    m_freq = new QDoubleSpinBox();
    m_freq->setRange(ModemEngine::kMinHz, ModemEngine::kMaxHz);
    m_freq->setDecimals(0);
    m_freq->setSuffix(tr(" Hz"));
    m_freq->setKeyboardTracking(false);
    m_freq->setToolTip(tr("Audio frequency of the signal (receive and transmit). Click the waterfall to tune."));
    r1->addWidget(m_freq);
    r1->addWidget(new QLabel(tr("RX trim")));
    m_trim = new QDoubleSpinBox();
    m_trim->setRange(-5.0, 5.0);
    m_trim->setDecimals(2);
    m_trim->setSingleStep(0.05);
    m_trim->setSuffix(tr(" %"));
    m_trim->setKeyboardTracking(false);
    m_trim->setToolTip(tr("Receive speed correction: if the text slants, nudge this until it is upright."));
    r1->addWidget(m_trim);
    r1->addWidget(new QLabel(tr("Zoom")));
    m_zoom = new QComboBox();
    for (int z = 1; z <= 4; ++z)
        m_zoom->addItem(QStringLiteral("%1x").arg(z), z);
    r1->addWidget(m_zoom);
    r1->addWidget(new QLabel(tr("Contrast")));
    m_contrast = new QSlider(Qt::Horizontal);
    m_contrast->setRange(5, 40);
    m_contrast->setMinimumWidth(70);
    r1->addWidget(m_contrast, 1);
    auto *clear = new QPushButton(tr("Clear"));
    r1->addWidget(clear);
    m_level = new QLabel();
    m_level->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("Level -100 dB")) + 4);
    r1->addWidget(m_level);
    v->addLayout(r1);

    v->addWidget(waterfall(), 1);
    v->addWidget(modeWarningLabel());
    v->addWidget(statusLabel());

    connect(m_decode, &QPushButton::toggled, m_hell, &ModemEngine::setDecoding);
    connect(m_freq, &QDoubleSpinBox::valueChanged, m_hell, &ModemEngine::setCarrierHz);
    connect(m_trim, &QDoubleSpinBox::valueChanged, m_hell, &HellEngine::setRateTrim);
    connect(m_zoom,
            &QComboBox::activated,
            this,
            [this](int i)
            {
                m_rx->setZoom(m_zoom->itemData(i).toInt());
                if (m_settings)
                    m_settings->setValue(QStringLiteral("Hell/Zoom"), m_rx->zoom());
            });
    connect(m_contrast,
            &QSlider::valueChanged,
            this,
            [this](int val)
            {
                m_rx->setGain(val / 10.0);
                if (m_settings)
                    m_settings->setValue(QStringLiteral("Hell/Contrast"), m_rx->gain());
            });
    connect(clear,
            &QPushButton::clicked,
            this,
            [this]()
            {
                m_rx->clear();
                m_echo->clear();
            });
    return top;
}

void HellPanel::saveState()
{
    ModemPanel::saveState();
}

void HellPanel::syncSettings()
{
    ModemPanel::syncSettings();
    const QSignalBlocker b1(m_decode), b2(m_freq), b3(m_trim);
    m_decode->setChecked(m_hell->decoding());
    if (!m_freq->hasFocus())
        m_freq->setValue(m_hell->carrierHz());
    m_trim->setValue(m_hell->rateTrim());
}
