#include "HellWidget.h"

#include "hellcore.h"

#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr int kGap = 4; // pixels between strips
const QColor kBack(16, 18, 20);
} // namespace

HellWidget::HellWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

QSize HellWidget::sizeHint() const
{
    return QSize(600, 4 * (stripHeight() + kGap));
}

QSize HellWidget::minimumSizeHint() const
{
    return QSize(150, stripHeight() + kGap);
}

int HellWidget::stripHeight() const
{
    return 2 * hell::kRows * m_zoom;
}

void HellWidget::clear()
{
    m_pending.clear();
    m_img = QImage(std::max(1, width()), std::max(1, height()), QImage::Format_RGB32);
    m_img.fill(kBack);
    m_x = 0;
    m_strip = 0;
    update();
}

void HellWidget::resizeEvent(QResizeEvent *)
{
    if (m_img.isNull())
    {
        clear();
        return;
    }
    // Keep the existing picture, anchored top-left.
    QImage n(std::max(1, width()), std::max(1, height()), QImage::Format_RGB32);
    n.fill(kBack);
    QPainter p(&n);
    p.drawImage(0, 0, m_img);
    p.end();
    m_img = n;
    const int maxStrips = std::max(1, m_img.height() / (stripHeight() + kGap));
    if (m_strip >= maxStrips)
    {
        m_strip = maxStrips - 1;
        m_x = 0;
    }
}

void HellWidget::setZoom(int z)
{
    z = std::clamp(z, 1, 4);
    if (z == m_zoom)
        return;
    m_zoom = z;
    clear();
    updateGeometry();
}

void HellWidget::setGain(double g)
{
    m_gain = std::clamp(g, 0.5, 4.0);
}

void HellWidget::newStrip()
{
    m_x = 0;
    const int sh = stripHeight() + kGap;
    const int maxStrips = std::max(1, m_img.height() / sh);
    if (m_strip + 1 < maxStrips)
    {
        ++m_strip;
        return;
    }
    // Scroll up one strip.
    const int bpl = int(m_img.bytesPerLine());
    const int moveRows = (maxStrips - 1) * sh;
    if (moveRows > 0)
        std::memmove(m_img.bits(), m_img.bits() + size_t(bpl) * size_t(sh), size_t(bpl) * size_t(moveRows));
    QPainter p(&m_img);
    p.fillRect(0, moveRows, m_img.width(), m_img.height() - moveRows, kBack);
}

void HellWidget::addColumns(const QVector<float> &cols)
{
    if (!isVisible())
    {
        // Hidden tab: keep the last minute and paint it when shown, at the real size.
        constexpr int maxPending = int(hell::kPixelRate * 60.0);
        m_pending += cols;
        if (m_pending.size() > maxPending)
            m_pending.remove(0, (m_pending.size() - maxPending) / hell::kRows * hell::kRows);
        return;
    }
    paintColumns(cols);
}

void HellWidget::showEvent(QShowEvent *)
{
    if (!m_pending.isEmpty())
    {
        const QVector<float> p = m_pending;
        m_pending.clear();
        paintColumns(p);
    }
}

void HellWidget::paintColumns(const QVector<float> &cols)
{
    if (m_img.isNull() || m_img.size() != size())
        resizeEvent(nullptr);
    const int n = int(cols.size()) / hell::kRows;
    const int cw = colWidth();
    const int sh = stripHeight() + kGap;
    for (int c = 0; c < n; ++c)
    {
        if (m_x + cw > m_img.width())
            newStrip();
        const int top = m_strip * sh;
        for (int copy = 0; copy < 2; ++copy)
            for (int r = 0; r < hell::kRows; ++r)
            {
                const double v = std::clamp(cols[c * hell::kRows + r] * m_gain - 0.15, 0.0, 1.0);
                const int g = int(std::pow(v, 0.8) * 235.0);
                const QRgb px
                    = qRgb(kBack.red() + g * 230 / 235, kBack.green() + g * 220 / 235, kBack.blue() + g * 160 / 235);
                // bottom-first column: row 0 at the bottom of each copy
                const int y0 = top + (copy == 0 ? hell::kRows : 2 * hell::kRows) * m_zoom - (r + 1) * m_zoom;
                for (int dy = 0; dy < m_zoom; ++dy)
                {
                    const int y = y0 + dy;
                    if (y < 0 || y >= m_img.height())
                        continue;
                    auto *line = reinterpret_cast<QRgb *>(m_img.scanLine(y));
                    for (int dx = 0; dx < cw && m_x + dx < m_img.width(); ++dx)
                        line[m_x + dx] = px;
                }
            }
        m_x += cw;
    }
    update();
}

void HellWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (m_img.isNull())
        p.fillRect(rect(), kBack);
    else
        p.drawImage(0, 0, m_img);
}
