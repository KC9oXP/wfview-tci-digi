#include "ImageView.h"

#include "sstvboxes.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <algorithm>

namespace
{
constexpr int kGripPx = 7; // corner handles: how close to a corner (screen pixels) grabs it

sstvbox::Frac toFrac(const QRectF &r)
{
    return sstvbox::Frac{r.x(), r.y(), r.width(), r.height()};
}
QRectF fromFrac(const sstvbox::Frac &b)
{
    return QRectF(b.x, b.y, b.w, b.h);
}
} // namespace

ImageView::ImageView(Fit fit, QWidget *parent)
    : QWidget(parent)
    , m_fit(fit)
{
    setSizePolicy(QSizePolicy::Expanding, fit == Fit::Width ? QSizePolicy::Preferred : QSizePolicy::Expanding);
    setMinimumSize(160, 120);
}

void ImageView::setImage(const QImage &img, int rowsShown)
{
    const bool grew = m_fit == Fit::Width && (img.height() != m_img.height() || rowsShown != m_rows);
    m_img = img;
    m_rows = rowsShown;
    if (grew)
        updateGeometry();
    update();
}

void ImageView::setColumnShift(int px)
{
    m_shift = m_img.isNull() ? 0 : ((px % m_img.width()) + m_img.width()) % m_img.width();
    update();
}

void ImageView::setPlaceholder(const QString &text)
{
    m_placeholder = text;
    update();
}

QImage ImageView::shownImage() const
{
    if (m_img.isNull())
        return QImage();
    const int rows = m_rows < 0 ? m_img.height() : std::clamp(m_rows, 0, m_img.height());
    QImage out = m_img.copy(0, 0, m_img.width(), std::max(1, rows));
    if (m_shift == 0)
        return out;
    QImage r(out.size(), out.format());
    QPainter p(&r);
    p.drawImage(0, 0, out, m_shift, 0, out.width() - m_shift, out.height());
    p.drawImage(out.width() - m_shift, 0, out, 0, 0, m_shift, out.height());
    return r;
}

QSize ImageView::sizeHint() const
{
    return m_fit == Fit::Width ? QSize(800, heightForWidth(800)) : QSize(480, 384);
}

int ImageView::heightForWidth(int w) const
{
    if (m_img.isNull() || m_img.width() == 0)
        return 120;
    const int rows = m_rows < 0 ? m_img.height() : std::max(1, m_rows);
    return std::max(120, int(double(rows) * w / m_img.width()));
}

QRect ImageView::target() const
{
    if (m_img.isNull())
        return QRect();
    const int rows = m_rows < 0 ? m_img.height() : std::max(1, m_rows);
    if (m_fit == Fit::Width)
        return QRect(0, 0, width(), int(double(rows) * width() / m_img.width()));
    // SSTV pictures are drawn at 4:3 whatever their pixel count.
    const double aspect = 4.0 / 3.0;
    int w = width(), h = int(w / aspect);
    if (h > height())
    {
        h = height();
        w = int(h * aspect);
    }
    return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

void ImageView::setMarkerLine(double fraction)
{
    const double v = fraction < 0.0 ? -1.0 : fraction;
    if (qFuzzyCompare(v + 2.0, m_marker + 2.0))
        return;
    m_marker = v;
    update();
}

void ImageView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(16, 16, 16));
    if (m_img.isNull())
    {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
        return;
    }
    const QRect t = target();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (m_fit == Fit::Window)
    {
        p.drawImage(t, m_img);
        if (showBoxes())
        {
            p.setRenderHint(QPainter::Antialiasing, false);
            for (int i = 0; i < m_boxes.size(); ++i)
            {
                const QRect r = boxOnScreen(i);
                if (r.isEmpty())
                    continue;
                // Dashed white on black so it shows on any picture; a number and four corner handles.
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor(0, 0, 0), 1));
                p.drawRect(r.adjusted(0, 0, -1, -1));
                QPen dash(QColor(255, 255, 255), 1, Qt::DashLine);
                p.setPen(dash);
                p.drawRect(r.adjusted(0, 0, -1, -1));
                const int hs = 6;
                for (const QPoint &c : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()})
                {
                    const QRect hr(c.x() - hs / 2, c.y() - hs / 2, hs, hs);
                    p.fillRect(hr, QColor(0, 0, 0));
                    p.fillRect(hr.adjusted(1, 1, -1, -1), QColor(255, 230, 0));
                }
                const QString n = QString::number(i + 1);
                const QRect lr(r.left() + 2, r.top() + 2, 12, 14);
                p.fillRect(lr, QColor(0, 0, 0, 170));
                p.setPen(QColor(255, 230, 0));
                p.drawText(lr, Qt::AlignCenter, n);
            }
        }
        if (m_marker >= 0.0)
        {
            // Sent part stays as is, the rest is dimmed; the line is yellow on a black edge so it shows on any picture.
            const int y = t.top() + int(std::min(m_marker, 1.0) * t.height());
            p.fillRect(QRect(t.left(), y, t.width(), t.bottom() - y + 1), QColor(0, 0, 0, 90));
            p.fillRect(QRect(t.left(), y - 3, t.width(), 7), QColor(0, 0, 0));
            p.fillRect(QRect(t.left(), y - 2, t.width(), 5), QColor(255, 230, 0));
        }
        return;
    }
    const QImage shown = shownImage();
    p.drawImage(QRect(0, 0, width(), int(double(shown.height()) * width() / shown.width())), shown);
}

void ImageView::setEditBoxes(const QVector<QRectF> &boxes)
{
    if (boxes == m_boxes)
        return;
    m_boxes = boxes;
    if (m_dragBox >= m_boxes.size() || (m_dragBox >= 0 && m_boxes.at(m_dragBox).isEmpty()))
        m_dragBox = -1;
    setMouseTracking(!m_boxes.isEmpty());
    update();
}

void ImageView::setBoxesEditable(bool on)
{
    if (on == m_boxEdit)
        return;
    m_boxEdit = on;
    if (!on && m_dragBox >= 0)
    {
        const int i = m_dragBox;
        m_dragBox = -1;
        emit boxReleased(i);
    }
    if (!on)
        unsetCursor();
    update();
}

bool ImageView::showBoxes() const
{
    return m_fit == Fit::Window && m_boxEdit && !m_img.isNull() && !m_boxes.isEmpty();
}

QRect ImageView::boxOnScreen(int i) const
{
    if (!showBoxes() || i < 0 || i >= m_boxes.size() || m_boxes.at(i).isEmpty())
        return QRect();
    const QRect t = target();
    const sstvbox::PixRect r = sstvbox::toPixels(toFrac(m_boxes.at(i)), t.width(), t.height());
    return QRect(t.left() + r.x, t.top() + r.y, r.w, r.h);
}

int ImageView::boxAt(const QPointF &pos, int *grip) const
{
    *grip = int(sstvbox::Grip::None);
    if (!showBoxes())
        return -1;
    const QRect t = target();
    if (t.width() <= 0 || t.height() <= 0)
        return -1;
    const double px = (pos.x() - t.left()) / t.width(), py = (pos.y() - t.top()) / t.height();
    const double gx = double(kGripPx) / t.width(), gy = double(kGripPx) / t.height();
    // Corners of any box first (a small box on top of a big one stays reachable), then insides, top-most first.
    for (int pass = 0; pass < 2; ++pass)
        for (int i = int(m_boxes.size()) - 1; i >= 0; --i)
        {
            if (m_boxes.at(i).isEmpty())
                continue;
            const sstvbox::Grip g = sstvbox::hit(toFrac(m_boxes.at(i)), px, py, gx, gy);
            if (g == sstvbox::Grip::None || (pass == 0) == (g == sstvbox::Grip::Move))
                continue;
            *grip = int(g);
            return i;
        }
    return -1;
}

void ImageView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragBox >= 0)
    {
        const QRect t = target();
        if (t.width() <= 0 || t.height() <= 0)
            return;
        const QPointF d = e->position() - m_dragFrom;
        const sstvbox::Frac b
            = sstvbox::drag(toFrac(m_dragStart), sstvbox::Grip(m_dragGrip), d.x() / t.width(), d.y() / t.height());
        const QRectF r = fromFrac(b);
        if (r != m_boxes.at(m_dragBox))
        {
            m_boxes[m_dragBox] = r;
            update();
            emit boxMoved(m_dragBox, r);
        }
        return;
    }
    int grip = 0;
    const int i = boxAt(e->position(), &grip);
    if (i < 0)
        unsetCursor();
    else
        switch (sstvbox::Grip(grip))
        {
        case sstvbox::Grip::TopLeft:
        case sstvbox::Grip::BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case sstvbox::Grip::TopRight:
        case sstvbox::Grip::BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        default:
            setCursor(Qt::SizeAllCursor);
        }
}

void ImageView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_dragBox < 0)
        return;
    const int i = m_dragBox;
    m_dragBox = -1;
    emit boxReleased(i);
}

void ImageView::mousePressEvent(QMouseEvent *e)
{
    if (m_img.isNull() || e->button() != Qt::LeftButton)
        return;
    int grip = 0;
    const int box = boxAt(e->position(), &grip);
    if (box >= 0)
    {
        m_dragBox = box;
        m_dragGrip = grip;
        m_dragFrom = e->position();
        m_dragStart = m_boxes.at(box);
        return;
    }
    const QRect t = target();
    if (t.isEmpty())
        return;
    const QPointF p = e->position();
    const int x = int((p.x() - t.left()) * m_img.width() / t.width());
    const int y = int((p.y() - t.top()) * m_img.height() / std::max(1, t.height()));
    emit clicked(QPoint(x, y));
}
