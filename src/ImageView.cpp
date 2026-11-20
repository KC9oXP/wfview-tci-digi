#include "ImageView.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

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

void ImageView::mousePressEvent(QMouseEvent *e)
{
    if (m_img.isNull() || e->button() != Qt::LeftButton)
        return;
    const QRect t = target();
    if (t.isEmpty())
        return;
    const QPointF p = e->position();
    const int x = int((p.x() - t.left()) * m_img.width() / t.width());
    const int y = int((p.y() - t.top()) * m_img.height() / std::max(1, t.height()));
    emit clicked(QPoint(x, y));
}
