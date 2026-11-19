#include "TxLevelSlider.h"

#include "txlevel.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>

TxLevelSlider::TxLevelSlider(QWidget *parent)
    : QWidget(parent)
{
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(txlevel::kMinSteps, txlevel::kMaxSteps);
    m_slider->setSingleStep(1); // arrow keys / wheel: 0.5 dB
    m_slider->setPageStep(3 * txlevel::kStepsPerDb); // Page Up/Down, click in the groove: 3 dB
    m_slider->setMinimumWidth(80);
    m_slider->setMaximumWidth(200);
    h->addWidget(m_slider, 1);
    m_value = new QLabel();
    m_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_value->setMinimumWidth(QFontMetrics(font()).horizontalAdvance(QStringLiteral("-40.0 dB")) + 4);
    h->addWidget(m_value);

    connect(m_slider,
            &QSlider::valueChanged,
            this,
            [this](int steps)
            {
                updateLabel();
                emit levelChanged(txlevel::stepsToLevel(steps));
            });
    updateLabel();
}

void TxLevelSlider::setLevel(double level)
{
    const QSignalBlocker b(m_slider);
    m_slider->setValue(txlevel::levelToSteps(level));
    updateLabel();
}

double TxLevelSlider::level() const
{
    return txlevel::stepsToLevel(m_slider->value());
}

void TxLevelSlider::setSliderToolTip(const QString &text)
{
    m_slider->setToolTip(text);
    m_value->setToolTip(text);
}

void TxLevelSlider::updateLabel()
{
    const double db = txlevel::stepsToDb(m_slider->value());
    m_value->setText(db == 0.0 ? QStringLiteral("0.0 dB") : QStringLiteral("%1 dB").arg(db, 0, 'f', 1));
}
