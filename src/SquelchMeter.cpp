#include "SquelchMeter.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace
{
SquelchMeter::Style g_style = SquelchMeter::Style::Bar;
QSet<SquelchMeter *> &meters()
{
    static QSet<SquelchMeter *> s;
    return s;
}
// Wedge of the magic eye: fully open (90 degrees) at no signal, a sliver at full scale.
constexpr double kEyeMaxWedge = 100.0;
constexpr double kEyeMinWedge = 3.0;
double wedgeFor(double frac)
{
    return kEyeMinWedge + (kEyeMaxWedge - kEyeMinWedge) * (1.0 - std::clamp(frac, 0.0, 1.0));
}
} // namespace

SquelchMeter::SquelchMeter(QWidget *parent)
    : QWidget(parent)
{
    meters().insert(this);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

SquelchMeter::~SquelchMeter()
{
    meters().remove(this);
}

SquelchMeter::Style SquelchMeter::style()
{
    return g_style;
}

void SquelchMeter::setStyleAll(Style s)
{
    if (s == g_style)
        return;
    g_style = s;
    for (SquelchMeter *m : std::as_const(meters()))
    {
        m->updateGeometry();
        m->update();
    }
}

SquelchMeter::Style SquelchMeter::styleFromString(const QString &s)
{
    return s.compare(QStringLiteral("eye"), Qt::CaseInsensitive) == 0 ? Style::Eye : Style::Bar;
}

QString SquelchMeter::styleToString(Style s)
{
    return s == Style::Eye ? QStringLiteral("eye") : QStringLiteral("bar");
}

QSize SquelchMeter::sizeHint() const
{
    const int h = fontMetrics().height();
    if (g_style == Style::Eye)
        return QSize(h + 6, h + 6);
    return QSize(6 * h, h);
}

QSize SquelchMeter::minimumSizeHint() const
{
    return sizeHint();
}

void SquelchMeter::setRange(double max)
{
    max = std::max(max, 1e-6);
    if (max == m_max)
        return;
    m_max = max;
    update();
}

void SquelchMeter::setReading(double level, double threshold, bool open, bool active)
{
    level = std::max(0.0, level);
    if (std::fabs(level - m_level) < 0.002 * m_max && threshold == m_threshold && open == m_open && active == m_active)
        return;
    m_level = level;
    m_threshold = threshold;
    m_open = open;
    m_active = active;
    update();
}

double SquelchMeter::fraction(double v) const
{
    return std::clamp(v / m_max, 0.0, 1.0);
}

void SquelchMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (g_style == Style::Eye)
        paintEye(p);
    else
        paintBar(p);
}

void SquelchMeter::paintBar(QPainter &p)
{
    const QRectF r = QRectF(rect()).adjusted(1.5, 2.5, -1.5, -2.5);
    const qreal radius = 3.0;
    const QPalette pal = palette();
    p.setPen(QPen(pal.color(QPalette::Mid), 1.0));
    p.setBrush(pal.color(QPalette::Base));
    p.drawRoundedRect(r, radius, radius);
    const QRectF inner = r.adjusted(1.0, 1.0, -1.0, -1.0);
    if (m_active)
    {
        // light green while closed, bright green while open
        const QColor fill = m_open ? QColor(0x50, 0xf0, 0x70) : QColor(0x90, 0xd8, 0x98, 150);
        const qreal w = inner.width() * fraction(m_level);
        if (w > 0.5)
        {
            QPainterPath clip;
            clip.addRoundedRect(inner, radius - 1.0, radius - 1.0);
            p.save();
            p.setClipPath(clip);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRect(QRectF(inner.left(), inner.top(), w, inner.height()));
            p.restore();
        }
    }
    if (m_threshold > 0.0)
    {
        const qreal x = inner.left() + inner.width() * fraction(m_threshold);
        p.setPen(QPen(QColor(0xff, 0xff, 0xff, m_active ? 235 : 120), 2.0));
        p.drawLine(QPointF(x, r.top() - 1.5), QPointF(x, r.bottom() + 1.5));
    }
}

void SquelchMeter::paintEye(QPainter &p)
{
    const qreal side = std::min(width(), height()) - 2.0;
    const QRectF outer((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    const QPointF c = outer.center();
    // bezel
    p.setPen(QPen(QColor(0x30, 0x30, 0x30), 1.5));
    p.setBrush(QColor(0x10, 0x14, 0x10));
    p.drawEllipse(outer);
    const QRectF screen = outer.adjusted(2.0, 2.0, -2.0, -2.0);

    // phosphor: dim until signal, brighter while open
    QColor glow = !m_active ? QColor(0x2a, 0x5a, 0x30) : m_open ? QColor(0x60, 0xff, 0x80) : QColor(0x48, 0xb0, 0x58);
    QRadialGradient g(c, screen.width() / 2.0);
    g.setColorAt(0.0, glow.darker(160));
    g.setColorAt(0.75, glow);
    g.setColorAt(1.0, glow.darker(130));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(screen);

    // dark shadow wedge at the top; closes as the level rises
    const double wedge = wedgeFor(m_active ? fraction(m_level) : 0.0);
    p.setBrush(QColor(0x08, 0x0c, 0x08));
    p.drawPie(screen, int((90.0 - wedge / 2.0) * 16.0), int(wedge * 16.0));

    // ticks where the wedge's edges stand at the squelch setting
    if (m_threshold > 0.0)
    {
        const double t = wedgeFor(fraction(m_threshold)) / 2.0;
        p.setPen(QPen(QColor(0xff, 0xff, 0xff, m_active ? 235 : 120), 1.6));
        const qreal r0 = outer.width() / 2.0 - 0.5, r1 = screen.width() / 2.0 - 2.5;
        for (double a : {90.0 - t, 90.0 + t})
        {
            const double rad = a * M_PI / 180.0;
            const QPointF dir(std::cos(rad), -std::sin(rad));
            p.drawLine(c + dir * r1, c + dir * r0);
        }
    }
    // centre cap
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x18, 0x18, 0x18));
    p.drawEllipse(c, screen.width() * 0.16, screen.width() * 0.16);
}
