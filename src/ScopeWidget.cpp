#include "ScopeWidget.h"

#include <QPainter>
#include <QResizeEvent>
#include <algorithm>

ScopeWidget::ScopeWidget(QWidget *parent)
    : QWidget(parent)
{
    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    setAttribute(Qt::WA_OpaquePaintEvent);
    m_idle = tr("Decode off");
}

QSize ScopeWidget::sizeHint() const
{
    return QSize(220, 220);
}

QSize ScopeWidget::minimumSizeHint() const
{
    return QSize(120, 120);
}

int ScopeWidget::side() const
{
    return std::max(16, std::min(width(), height()) - 2);
}

void ScopeWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int s = side();
    if (m_img.width() != s)
    {
        m_img = QImage(s, s, QImage::Format_RGB32);
        m_img.fill(Qt::black);
    }
}

void ScopeWidget::setIdleText(const QString &text)
{
    if (text == m_idle)
        return;
    m_idle = text;
    if (!m_img.isNull())
        m_img.fill(Qt::black);
    update();
}

void ScopeWidget::addPoints(const QVector<float> &xy)
{
    if (!m_idle.isEmpty())
        return;
    decayAndDraw(xy);
    update();
}

void ScopeWidget::decayAndDraw(const QVector<float> &xy)
{
    if (m_img.isNull())
        return;
    QPainter p(&m_img);
    const int s = m_img.width();
    // phosphor decay: each frame dims what is already on the screen
    p.fillRect(0, 0, s, s, QColor(0, 0, 0, int(0.28 * 255)));
    p.setPen(QColor(40, 140, 70, int(0.30 * 255)));
    p.drawLine(s / 2, 0, s / 2, s);
    p.drawLine(0, s / 2, s, s / 2);

    const double c = s / 2.0;
    const double r = s / 2.0 - 3.0;
    const QColor glow(40, 255, 100, int(0.22 * 255));
    const QColor core(0x8d, 0xff, 0xa8);
    for (int i = 0; i + 1 < xy.size(); i += 2)
        p.fillRect(QRectF(c + xy[i] * r - 1.5, c - xy[i + 1] * r - 1.5, 3.0, 3.0), glow);
    for (int i = 0; i + 1 < xy.size(); i += 2)
        p.fillRect(QRectF(c + xy[i] * r - 0.6, c - xy[i + 1] * r - 0.6, 1.2, 1.2), core);
}

void ScopeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    const int s = side();
    const QRect sq((width() - s) / 2, (height() - s) / 2, s, s);
    if (m_img.isNull() || m_img.width() != s)
    {
        m_img = QImage(s, s, QImage::Format_RGB32);
        m_img.fill(Qt::black);
    }
    p.drawImage(sq.topLeft(), m_img);
    p.setPen(QColor(40, 110, 60));
    p.drawRect(sq.adjusted(0, 0, -1, -1));
    if (!m_idle.isEmpty())
    {
        p.setPen(QColor(0x2f, 0x7f, 0x45));
        p.drawText(sq, Qt::AlignCenter, m_idle);
    }
}
