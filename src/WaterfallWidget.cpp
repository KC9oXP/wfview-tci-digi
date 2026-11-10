#include "WaterfallWidget.h"

#include <QContextMenuEvent>
#include <QDateTime>
#include <QKeyEvent>
#include <QMenu>
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
    setFocusPolicy(Qt::ClickFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip(tr("Click a signal to tune to it. Mouse wheel: fine tune 1 Hz.\n"
                  "Shift+wheel or PgUp/PgDn: scroll back through the last few minutes; Esc: back to live.\n"
                  "Right-click: save a picture of the waterfall."));
    m_times.resize(kHistRows);
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

int WaterfallWidget::visibleRows() const
{
    return std::max(1, height() - scaleHeight());
}

void WaterfallWidget::clearImage()
{
    m_img = QImage(std::max(1, width()), kHistRows, QImage::Format_RGB32);
    m_img.fill(Qt::black);
    m_head = 0;
    m_count = 0;
    m_offset = 0;
    update();
}

void WaterfallWidget::goLive()
{
    if (m_offset == 0)
        return;
    m_offset = 0;
    update();
}

void WaterfallWidget::scrollBy(int rows)
{
    const int maxOff = std::max(0, m_count - visibleRows());
    m_offset = std::clamp(m_offset + rows, 0, maxOff);
    update();
}

void WaterfallWidget::resizeEvent(QResizeEvent *)
{
    // Keep the history, stretched to the new width.
    if (m_img.isNull())
        clearImage();
    else if (m_img.width() != std::max(1, width()))
        m_img = m_img.scaled(std::max(1, width()), kHistRows).convertToFormat(QImage::Format_RGB32);
    m_offset = std::clamp(m_offset, 0, std::max(0, m_count - visibleRows()));
}

void WaterfallWidget::addLine(const QVector<float> &db, double hzPerBin)
{
    if (db.isEmpty() || hzPerBin <= 0.0)
        return;
    if (m_img.isNull() || m_img.width() != std::max(1, width()))
        resizeEvent(nullptr);
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

    // The ring grows downwards: the new line goes just above the previous newest.
    m_head = (m_head + kHistRows - 1) % kHistRows;
    m_count = std::min(kHistRows, m_count + 1);
    m_times[m_head] = QDateTime::currentMSecsSinceEpoch();
    auto *row = reinterpret_cast<QRgb *>(m_img.scanLine(m_head));
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
    if (m_offset > 0) // hold the view still while scrolled back
        m_offset = std::min(m_offset + 1, std::max(0, m_count - visibleRows()));
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
    {
        // Newest-first from the ring, starting m_offset lines back, in up to two slices.
        const int vis = visibleRows();
        int y = 0;
        int logical = m_offset;
        while (y < vis && logical < m_count)
        {
            const int phys = (m_head + logical) % kHistRows;
            const int n = std::min({vis - y, kHistRows - phys, m_count - logical});
            p.drawImage(QRect(0, sh + y, m_img.width(), n), m_img, QRect(0, phys, m_img.width(), n));
            y += n;
            logical += n;
        }
        if (y < vis)
            p.fillRect(0, sh + y, width(), vis - y, Qt::black);
    }

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

    if (m_offset > 0 && m_count > 0)
    {
        const qint64 newest = m_times[m_head];
        const qint64 shown = m_times[(m_head + m_offset) % kHistRows];
        const int sec = int((newest - shown) / 1000);
        const QString t = tr("History  -%1:%2   (Esc: live)").arg(sec / 60).arg(sec % 60, 2, 10, QLatin1Char('0'));
        const int tw = fontMetrics().horizontalAdvance(t);
        p.fillRect(4, sh + 4, tw + 8, fontMetrics().height() + 2, QColor(120, 70, 0, 200));
        p.setPen(Qt::white);
        p.drawText(8, sh + 4 + fontMetrics().ascent() + 1, t);
    }

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
    // Some platforms turn Shift+wheel into a horizontal wheel.
    const int dy = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    const int steps = dy / 120;
    if (steps != 0)
    {
        if (e->modifiers() & Qt::ShiftModifier)
            scrollBy(steps * 20); // up = back in time
        else
            emit nudged(m_marker + steps);
    }
    e->accept();
}

void WaterfallWidget::keyPressEvent(QKeyEvent *e)
{
    switch (e->key())
    {
    case Qt::Key_PageUp:
        scrollBy(visibleRows() * 3 / 4);
        break;
    case Qt::Key_PageDown:
        scrollBy(-visibleRows() * 3 / 4);
        break;
    case Qt::Key_Escape:
    case Qt::Key_End:
        goLive();
        break;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}

void WaterfallWidget::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    QAction *live = menu.addAction(tr("Live"), this, &WaterfallWidget::goLive);
    live->setEnabled(m_offset > 0);
    menu.addAction(tr("Back 1 minute"),
                   this,
                   [this]()
                   {
                       // lines per second from the timestamps of the newest lines
                       if (m_count < 20)
                           return;
                       const double dt = double(m_times[m_head] - m_times[(m_head + 19) % kHistRows]) / 19000.0;
                       if (dt > 0)
                           scrollBy(int(60.0 / dt));
                   });
    menu.addSeparator();
    menu.addAction(tr("Save picture..."), this, &WaterfallWidget::saveRequested);
    menu.exec(e->globalPos());
}
