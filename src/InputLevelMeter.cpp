#include "InputLevelMeter.h"

#include "inputlevel.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

InputLevelMeter::InputLevelMeter(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setToolTip(tr("RX input level: the receive audio wfview sends to this app, the same on every tab.\n"
                  "The bar is the average (RMS) level, the thin mark the recent peak, in dBFS (0 = full scale).\n"
                  "Green -50 to -12 dBFS is about right. Amber: too quiet (below -50) or too hot (above -12); "
                  "red: peaks at full scale (clipping).\n"
                  "Set it with wfview's RX audio level and the radio's AF/RF gain. It is not a signal report: "
                  "band noise has a level too (SNR, left, tells you whether a signal is there).\n"
                  "Shows TX while transmitting (wfview sends no receive audio then)."));
}

QSize InputLevelMeter::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(QStringLiteral("-88 dBFS")) + 40, fm.height() + 4);
}

QSize InputLevelMeter::minimumSizeHint() const
{
    return sizeHint();
}

void InputLevelMeter::setLevel(double rmsDb, double peakHoldDb)
{
    if (!m_idle && std::lround(rmsDb) == std::lround(m_rmsDb) && std::fabs(peakHoldDb - m_peakDb) < 0.5)
        return;
    m_idle = false;
    m_rmsDb = rmsDb;
    m_peakDb = peakHoldDb;
    update();
}

void InputLevelMeter::setIdle(const QString &word)
{
    if (m_idle && m_word == word)
        return;
    m_idle = true;
    m_word = word;
    update();
}

void InputLevelMeter::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1.5, 2.5, -1.5, -2.5);
    const qreal radius = 3.0;
    const QPalette pal = palette();

    // track
    QColor track = pal.color(QPalette::Base);
    QColor edge = pal.color(QPalette::Mid);
    p.setPen(QPen(edge, 1.0));
    p.setBrush(track);
    p.drawRoundedRect(r, radius, radius);

    QColor textColor = pal.color(QPalette::WindowText);
    QString text;
    if (m_idle)
    {
        textColor = pal.color(QPalette::Disabled, QPalette::WindowText);
        text = m_word;
    }
    else
    {
        QColor fill = QColor(0x50, 0xd0, 0x70);
        switch (inlevel::zoneFor(m_rmsDb, m_peakDb))
        {
        case inlevel::Zone::Good:
            fill = QColor(0x50, 0xd0, 0x70); // same green as SNR
            break;
        case inlevel::Zone::Quiet:
        case inlevel::Zone::Hot:
            fill = QColor(0xe0, 0xa0, 0x30); // same amber as SNR
            break;
        case inlevel::Zone::Clip:
            fill = QColor(0xe0, 0x50, 0x40);
            break;
        }
        // muted fill so the number on top stays readable
        QColor body = fill;
        body.setAlpha(110);
        const QRectF inner = r.adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal w = inner.width() * inlevel::fraction(m_rmsDb);
        if (w > 0.5)
        {
            QPainterPath clip;
            clip.addRoundedRect(inner, radius - 1.0, radius - 1.0);
            p.save();
            p.setClipPath(clip);
            p.setPen(Qt::NoPen);
            p.setBrush(body);
            p.drawRect(QRectF(inner.left(), inner.top(), w, inner.height()));
            p.restore();
        }
        // peak-hold tick
        if (m_peakDb > inlevel::kFloorDb)
        {
            const qreal x = inner.left() + inner.width() * inlevel::fraction(m_peakDb);
            p.setPen(QPen(fill, 2.0));
            p.drawLine(QPointF(x, inner.top() + 1.0), QPointF(x, inner.bottom() - 1.0));
        }
        text = tr("%1 dBFS").arg(std::lround(std::max(m_rmsDb, -99.0)));
    }

    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    p.setFont(f);
    p.setPen(textColor);
    p.drawText(r, Qt::AlignCenter, text);
}
