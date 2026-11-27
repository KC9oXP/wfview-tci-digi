#include "SquelchControl.h"

#include <QCheckBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>

#include <algorithm>
#include <cmath>

SquelchControl::SquelchControl(const Scale &nativeScale, const Scale &dbScale, bool dbOnly, QWidget *parent)
    : QWidget(parent)
    , m_native(nativeScale)
    , m_db(dbScale)
    , m_dbOnly(dbOnly)
    , m_inDb(dbOnly)
{
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    h->addWidget(new QLabel(tr("Squelch")));
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setObjectName(QStringLiteral("squelchSlider"));
    m_slider->setMinimumWidth(70);
    h->addWidget(m_slider, 1);
    m_value = new QLabel();
    m_value->setObjectName(QStringLiteral("squelchValue"));
    const QFontMetrics fm(font());
    m_value->setMinimumWidth(
        std::max(fm.horizontalAdvance(QStringLiteral("30.0 dB")), fm.horizontalAdvance(QStringLiteral("off"))) + 4);
    h->addWidget(m_value);
    m_dbBox = new QCheckBox(tr("dB"));
    m_dbBox->setObjectName(QStringLiteral("squelchDb"));
    if (m_dbOnly)
    {
        m_dbBox->setChecked(true);
        m_dbBox->setEnabled(false);
        m_dbBox->setToolTip(tr("This mode's squelch is a signal-to-noise in dB already."));
    }
    else
        m_dbBox->setToolTip(tr("Ticked: the squelch is a signal-to-noise in dB, measured by this decoder. "
                               "Unticked: the mode's own measure (see the slider's tooltip). Each keeps its own "
                               "setting."));
    h->addWidget(m_dbBox);
    m_meter = new SquelchMeter();
    m_meter->setObjectName(QStringLiteral("squelchMeter"));
    h->addWidget(m_meter);
    applyScale();

    connect(m_slider,
            &QSlider::valueChanged,
            this,
            [this](int v)
            {
                showValue();
                emit valueChanged(v * scale().step);
            });
    connect(m_dbBox,
            &QCheckBox::toggled,
            this,
            [this](bool on)
            {
                if (m_dbOnly)
                    return;
                m_inDb = on;
                emit inDbToggled(on); // the owner answers with setSetting() for the other scale
            });
}

double SquelchControl::value() const
{
    return m_slider->value() * scale().step;
}

void SquelchControl::applyScale()
{
    const QSignalBlocker b(m_slider);
    const Scale &s = scale();
    m_slider->setRange(0, int(std::lround(s.max / s.step)));
    m_slider->setSingleStep(1);
    m_slider->setPageStep(std::max(1, int(std::lround(s.max / s.step / 10.0))));
    m_slider->setToolTip(s.tip + tr("\n0 turns the squelch off: everything prints, noise included."));
    m_meter->setRange(s.max);
}

void SquelchControl::setSetting(double value, bool inDb)
{
    if (m_dbOnly)
        inDb = true;
    m_inDb = inDb;
    {
        const QSignalBlocker b(m_dbBox);
        m_dbBox->setChecked(inDb);
    }
    applyScale();
    {
        const QSignalBlocker b(m_slider);
        m_slider->setValue(int(std::lround(std::clamp(value, 0.0, scale().max) / scale().step)));
    }
    showValue();
}

QString SquelchControl::text(double v) const
{
    return QString::number(v, 'f', scale().decimals) + scale().unit;
}

void SquelchControl::showValue()
{
    const double v = value();
    m_value->setText(v > 0.0 ? text(v) : tr("off"));
    m_meter->setReading(m_level, v, m_open, m_active);
    QString tip;
    if (!m_active)
        tip = tr("Squelch meter: shows the level once decoding is on.");
    else if (!(v > 0.0))
        tip = tr("Level %1, squelch off (everything prints)").arg(text(m_level));
    else
        tip = tr("Level %1, squelch %2: %3")
                  .arg(text(m_level), text(v), m_open ? tr("open (printing)") : tr("closed"));
    m_meter->setToolTip(tip);
}

void SquelchControl::setReading(double level, bool open, bool active)
{
    m_level = std::max(0.0, level);
    m_open = open;
    m_active = active;
    showValue();
}
