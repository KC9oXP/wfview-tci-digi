#include "HellWidget.h"

#include "hellcore.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int kGap = 4; // pixels between strips
const QColor kBack(16, 18, 20);
constexpr int kAlignEvery = 35; // columns (2 s) between automatic alignment checks
} // namespace

HellWidget::HellWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::ClickFocus);
    m_live = new QPushButton(tr("Live"), this);
    m_live->setToolTip(tr("Back to the newest text (End)."));
    m_live->setCursor(Qt::PointingHandCursor);
    m_live->setStyleSheet(QStringLiteral("QPushButton { background-color: rgba(120, 70, 0, 220); color: white;"
                                         " border: 1px solid #c08030; border-radius: 3px; padding: 2px 10px; }"));
    m_live->hide();
    connect(m_live, &QPushButton::clicked, this, &HellWidget::goLive);
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

int HellWidget::columnsPerStrip() const
{
    return std::max(1, width() / colWidth());
}

int HellWidget::visibleStrips() const
{
    return std::max(1, height() / (stripHeight() + kGap));
}

int HellWidget::rowShift() const
{
    const int s = (m_autoAlign ? m_autoShift : 0) + m_manual;
    return ((s % hell::kRows) + hell::kRows) % hell::kRows;
}

qint64 HellWidget::firstColumn() const
{
    const qint64 s = rowShift();
    return std::max<qint64>(0, (m_dropped - s + hell::kRows - 1) / hell::kRows);
}

qint64 HellWidget::endColumn() const
{
    const qint64 s = rowShift();
    const qint64 n = m_dropped + qint64(m_px.size()) - s;
    return n <= 0 ? 0 : n / hell::kRows;
}

qint64 HellWidget::newestStrip() const
{
    const qint64 end = endColumn();
    const qint64 first = std::max<qint64>(m_colBase, end > 0 ? end - 1 : 0);
    return std::max<qint64>(0, (first - m_colBase) / columnsPerStrip());
}

float HellWidget::value(qint64 col, int row) const
{
    const qint64 i = rowShift() + col * hell::kRows + row - m_dropped;
    if (i < 0 || i >= qint64(m_px.size()))
        return 0.0f;
    return m_px[int(i)];
}

void HellWidget::clear()
{
    m_colBase = endColumn();
    m_dropped += m_px.size();
    m_px.clear();
    m_back = 0;
    m_lastNewest = -1;
    m_candidate = -1;
    m_sinceAlign = 0;
    m_live->hide();
    m_dirty = true;
    update();
}

void HellWidget::setZoom(int z)
{
    z = std::clamp(z, 1, 4);
    if (z == m_zoom)
        return;
    m_zoom = z;
    m_back = 0;
    m_live->hide();
    m_dirty = true;
    updateGeometry();
    update();
}

void HellWidget::setGain(double g)
{
    m_gain = std::clamp(g, 0.5, 4.0);
    m_dirty = true;
    update();
}

void HellWidget::setAutoAlign(bool on)
{
    if (on == m_autoAlign)
        return;
    m_autoAlign = on;
    if (on)
        updateAutoShift();
    m_dirty = true;
    update();
    emit alignmentChanged();
}

void HellWidget::setManualShift(int halfPixels)
{
    halfPixels = std::clamp(halfPixels, -hell::kRows / 2, hell::kRows / 2);
    if (halfPixels == m_manual)
        return;
    m_manual = halfPixels;
    m_dirty = true;
    update();
    emit alignmentChanged();
}

void HellWidget::updateAutoShift()
{
    m_sinceAlign = 0;
    const int local = hell::bestRowShift(m_px.constData(), size_t(m_px.size()));
    if (local < 0)
        return;
    const int abs = int((m_dropped + local) % hell::kRows);
    if (abs == m_autoShift)
    {
        m_candidate = -1;
        return;
    }
    // Change only when two checks in a row agree.
    if (abs != m_candidate)
    {
        m_candidate = abs;
        return;
    }
    m_candidate = -1;
    m_autoShift = abs;
    if (m_autoAlign)
    {
        m_dirty = true;
        update();
        emit alignmentChanged();
    }
}

void HellWidget::addColumns(const QVector<float> &cols)
{
    if (cols.isEmpty())
        return;
    m_px += cols;
    const int keep = int(hell::kPixelRate * kKeepSeconds);
    if (m_px.size() > keep + int(hell::kPixelRate * 60))
    {
        const int drop = m_px.size() - keep;
        m_px.remove(0, drop);
        m_dropped += drop;
    }
    m_sinceAlign += cols.size() / hell::kRows;
    if (m_sinceAlign >= kAlignEvery)
        updateAutoShift();

    // A scrolled-back view stays on the same text while new strips arrive.
    const qint64 ns = newestStrip();
    if (m_back > 0 && m_lastNewest >= 0 && ns > m_lastNewest)
        m_back += int(ns - m_lastNewest);
    m_lastNewest = ns;
    m_dirty = true;
    if (isVisible())
        update();
}

void HellWidget::scrollStrips(int n)
{
    const qint64 firstStrip = std::max<qint64>(0, (firstColumn() - m_colBase) / columnsPerStrip());
    const int maxBack = int(std::max<qint64>(0, newestStrip() - firstStrip + 1 - visibleStrips()));
    const int before = m_back;
    m_back = std::clamp(m_back + n, 0, maxBack);
    if (m_back == before)
        return;
    m_live->setVisible(m_back > 0);
    placeLive();
    m_dirty = true;
    update();
    if ((before == 0) != (m_back == 0))
        emit liveChanged(m_back == 0);
}

void HellWidget::goLive()
{
    if (m_back == 0)
        return;
    m_back = 0;
    m_live->hide();
    m_dirty = true;
    update();
    emit liveChanged(true);
}

void HellWidget::placeLive()
{
    m_live->adjustSize();
    m_live->move(width() - m_live->width() - 8, height() - m_live->height() - 6);
    m_live->raise();
}

void HellWidget::resizeEvent(QResizeEvent *)
{
    m_dirty = true;
    scrollStrips(0); // keep the scrolled-back position valid
    placeLive();
}

void HellWidget::wheelEvent(QWheelEvent *e)
{
    const int dy = e->angleDelta().y();
    const int steps = dy / 120;
    if (steps != 0)
        scrollStrips(steps); // up = back in time
    e->accept();
}

void HellWidget::keyPressEvent(QKeyEvent *e)
{
    switch (e->key())
    {
    case Qt::Key_PageUp:
        scrollStrips(std::max(1, visibleStrips() - 1));
        break;
    case Qt::Key_PageDown:
        scrollStrips(-std::max(1, visibleStrips() - 1));
        break;
    case Qt::Key_Up:
        scrollStrips(1);
        break;
    case Qt::Key_Down:
        scrollStrips(-1);
        break;
    case Qt::Key_End:
    case Qt::Key_Escape:
        goLive();
        break;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}

void HellWidget::drawColumn(QImage &img, int x, int top, qint64 col, int zoom) const
{
    const int cw = 2 * zoom;
    for (int copy = 0; copy < 2; ++copy)
        for (int r = 0; r < hell::kRows; ++r)
        {
            const double v = std::clamp(value(col, r) * m_gain - 0.15, 0.0, 1.0);
            const int g = int(std::pow(v, 0.8) * 235.0);
            const QRgb px
                = qRgb(kBack.red() + g * 230 / 235, kBack.green() + g * 220 / 235, kBack.blue() + g * 160 / 235);
            // bottom-first column: row 0 at the bottom of each copy
            const int y0 = top + (copy == 0 ? hell::kRows : 2 * hell::kRows) * zoom - (r + 1) * zoom;
            for (int dy = 0; dy < zoom; ++dy)
            {
                const int y = y0 + dy;
                if (y < 0 || y >= img.height())
                    continue;
                auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
                for (int dx = 0; dx < cw && x + dx < img.width(); ++dx)
                    line[x + dx] = px;
            }
        }
}

QImage HellWidget::render(qint64 from, qint64 to, int perStrip, int zoom) const
{
    from = std::max(from, firstColumn());
    to = std::min(to, endColumn());
    perStrip = std::max(1, perStrip);
    zoom = std::clamp(zoom, 1, 4);
    if (to <= from)
        return QImage();
    const int strips = int((to - from + perStrip - 1) / perStrip);
    const int sh = 2 * hell::kRows * zoom + kGap;
    QImage img(perStrip * 2 * zoom, strips * sh, QImage::Format_RGB32);
    img.fill(kBack);
    for (qint64 c = from; c < to; ++c)
    {
        const int k = int((c - from) / perStrip);
        drawColumn(img, int((c - from) % perStrip) * 2 * zoom, k * sh, c, zoom);
    }
    return img;
}

void HellWidget::paintEvent(QPaintEvent *)
{
    if (m_dirty || m_canvas.size() != size())
    {
        if (m_canvas.size() != size())
            m_canvas = QImage(std::max(1, width()), std::max(1, height()), QImage::Format_RGB32);
        m_canvas.fill(kBack);
        const int cps = columnsPerStrip();
        const int vis = visibleStrips();
        const qint64 newest = newestStrip();
        const qint64 last = newest - m_back;
        const qint64 first = (last + 1 < vis) ? 0 : last - vis + 1;
        const qint64 c0 = firstColumn(), c1 = endColumn();
        const int sh = stripHeight() + kGap;
        for (qint64 k = first; k <= last; ++k)
        {
            const int top = int(k - first) * sh;
            const qint64 a = std::max(c0, m_colBase + k * cps);
            const qint64 b = std::min(c1, m_colBase + (k + 1) * cps);
            for (qint64 c = a; c < b; ++c)
                drawColumn(m_canvas, int(c - m_colBase - k * cps) * colWidth(), top, c, m_zoom);
        }
        m_dirty = false;
    }
    QPainter p(this);
    p.drawImage(0, 0, m_canvas);
}
