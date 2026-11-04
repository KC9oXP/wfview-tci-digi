#include "WaterfallWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr double kRangeDb = 45.0; // colour span above the noise floor
}

WaterfallWidget::WaterfallWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip(tr("Click a signal to tune to it. Mouse wheel: fine tune 1 Hz."));
    // black -> blue -> cyan -> yellow -> red -> white
    const struct
    {
        double p;
        int r, g, b;
    } stops[] = {{0.0, 0, 0, 0},
                 {0.25, 0, 0, 160},
                 {0.45, 0, 160, 200},
                 {0.65, 230, 220, 0},
                 {0.85, 230, 40, 0},
                 {1.0, 255, 255, 255}};
    m_palette.resize(256);
    for (int i = 0; i < 256; ++i)
    {
        const double p = i / 255.0;
        int s = 0;
        while (s < 4 && p > stops[s + 1].p)
            ++s;
        const double f = (p - stops[s].p) / (stops[s + 1].p - stops[s].p);
        m_palette[i] = qRgb(int(stops[s].r + f * (stops[s + 1].r - stops[s].r)),
                            int(stops[s].g + f * (stops[s + 1].g - stops[s].g)),
                            int(stops[s].b + f * (stops[s + 1].b - stops[s].b)));
    }
}

QSize WaterfallWidget::sizeHint() const
{
    return QSize(600, 160);
}

QSize WaterfallWidget::minimumSizeHint() const
{
    return QSize(200, 70);
}

int WaterfallWidget::scaleHeight() const
{
    return fontMetrics().height() + 6;
}

double WaterfallWidget::xToHz(double x) const
{
    return m_minHz + (m_maxHz - m_minHz) * x / std::max(1, width());
}

double WaterfallWidget::hzToX(double hz) const
{
    return (hz - m_minHz) / (m_maxHz - m_minHz) * width();
}

QRgb WaterfallWidget::colour(double v) const
{
    return m_palette[std::clamp(int(v * 255.0), 0, 255)];
}

void WaterfallWidget::clearImage()
{
    const int h = std::max(1, height() - scaleHeight());
    m_img = QImage(std::max(1, width()), h, QImage::Format_RGB32);
    m_img.fill(Qt::black);
    update();
}

void WaterfallWidget::resizeEvent(QResizeEvent *)
{
    // Keep what is there, stretched to the new size.
    const int h = std::max(1, height() - scaleHeight());
    if (m_img.isNull())
        clearImage();
    else
        m_img = m_img.scaled(std::max(1, width()), h).convertToFormat(QImage::Format_RGB32);
}

void WaterfallWidget::addLine(const QVector<float> &db, double hzPerBin)
{
    if (db.isEmpty() || hzPerBin <= 0.0)
        return;
    if (m_img.isNull() || m_img.width() != width())
        clearImage();
    const int w = m_img.width();

    // Noise floor: the 20th percentile of this line, smoothed.
    QVector<float> sorted = db;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 5, sorted.end());
    const double p20 = sorted[sorted.size() / 5];
    if (!m_floorSet)
    {
        m_floor = p20;
        m_floorSet = true;
    }
    m_floor += 0.05 * (p20 - m_floor);

    // Scroll down one line and draw the new one at the top.
    const int bpl = int(m_img.bytesPerLine());
    uchar *bits = m_img.bits();
    std::memmove(bits + bpl, bits, size_t(bpl) * size_t(m_img.height() - 1));
    auto *row = reinterpret_cast<QRgb *>(m_img.scanLine(0));
    for (int x = 0; x < w; ++x)
    {
        const double f0 = xToHz(x), f1 = xToHz(x + 1);
        int b0 = int(f0 / hzPerBin), b1 = std::max(b0, int(f1 / hzPerBin));
        b0 = std::clamp(b0, 0, int(db.size()) - 1);
        b1 = std::clamp(b1, 0, int(db.size()) - 1);
        float v = db[b0];
        for (int b = b0 + 1; b <= b1; ++b)
            v = std::max(v, db[b]);
        row[x] = colour((v - m_floor + 3.0) / kRangeDb);
    }
    update();
}

void WaterfallWidget::setMarker(double hz, double bandwidthHz)
{
    if (std::fabs(hz - m_marker) < 0.05 && std::fabs(bandwidthHz - m_bw) < 0.05)
        return;
    m_marker = hz;
    m_bw = bandwidthHz;
    update();
}

void WaterfallWidget::setTransmitting(bool on)
{
    m_tx = on;
    update();
}

void WaterfallWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int sh = scaleHeight();
    p.fillRect(0, 0, width(), sh, palette().window());
    if (!m_img.isNull())
        p.drawImage(0, sh, m_img);

    // scale
    p.setPen(palette().windowText().color());
    for (int hz = 100 * int(std::ceil(m_minHz / 100.0)); hz <= m_maxHz; hz += 100)
    {
        const int x = int(hzToX(hz));
        const bool major = hz % 500 == 0;
        p.drawLine(x, sh - (major ? 6 : 3), x, sh - 1);
        if (major && hz > m_minHz && hz < m_maxHz)
        {
            const QString t = QString::number(hz);
            const int tw = fontMetrics().horizontalAdvance(t);
            p.drawText(x - tw / 2, fontMetrics().ascent() + 1, t);
        }
    }

    // carrier marker
    const QColor mc = m_tx ? QColor(255, 60, 60) : QColor(255, 200, 0);
    const int xm = int(hzToX(m_marker));
    const int half = std::max(2, int(hzToX(m_marker + m_bw / 2.0)) - xm);
    p.setPen(QPen(mc, 1));
    p.drawLine(xm - half, sh, xm - half, height());
    p.drawLine(xm + half, sh, xm + half, height());
    p.setPen(QPen(mc, 2));
    p.drawLine(xm - half, sh, xm + half, sh);
    QPolygon tri;
    tri << QPoint(xm - 5, 0) << QPoint(xm + 5, 0) << QPoint(xm, 6);
    p.setBrush(mc);
    p.drawPolygon(tri);

    if (m_hoverX >= 0)
    {
        p.setPen(QPen(QColor(255, 255, 255, 120), 1, Qt::DashLine));
        p.drawLine(m_hoverX, sh, m_hoverX, height());
        const QString t = QStringLiteral("%1 Hz").arg(qRound(xToHz(m_hoverX)));
        const int tw = fontMetrics().horizontalAdvance(t);
        const int tx = std::clamp(m_hoverX + 6, 0, std::max(0, width() - tw - 4));
        p.fillRect(tx - 2, sh + 2, tw + 4, fontMetrics().height(), QColor(0, 0, 0, 160));
        p.setPen(Qt::white);
        p.drawText(tx, sh + 2 + fontMetrics().ascent(), t);
    }
}

void WaterfallWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        emit clicked(std::round(xToHz(e->position().x())));
}

void WaterfallWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_hoverX = int(e->position().x());
    update();
}

void WaterfallWidget::leaveEvent(QEvent *)
{
    m_hoverX = -1;
    update();
}

void WaterfallWidget::wheelEvent(QWheelEvent *e)
{
    const int steps = e->angleDelta().y() / 120;
    if (steps != 0)
        emit nudged(m_marker + steps);
    e->accept();
}
