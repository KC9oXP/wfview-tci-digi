#include "FreqEdit.h"

#include "freqdigits.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QWheelEvent>

namespace
{
constexpr int kHoldMs = 700; // ignore the radio's reports this long after a wheel/click step
}

FreqEdit::FreqEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setMouseTracking(true);
    setAlignment(Qt::AlignRight);
}

QString FreqEdit::format(qint64 hz)
{
    // 14074000 -> "14.074.000"
    QString s = QString::number(hz);
    for (int i = s.size() - 3; i > 0; i -= 3)
        s.insert(i, QLatin1Char('.'));
    return s;
}

void FreqEdit::showHz(qint64 hz)
{
    if (hasFocus() || hz <= 0 || QDateTime::currentMSecsSinceEpoch() < m_holdUntilMs)
        return;
    const QString t = format(hz);
    if (t != text())
        setText(t);
}

bool FreqEdit::digitMode() const
{
    // While typing, the box behaves like any text box.
    return isEnabled() && !hasFocus() && shownHz() > 0;
}

qint64 FreqEdit::shownHz() const
{
    QString t = text();
    t.remove(QLatin1Char('.'));
    bool ok = false;
    const qint64 hz = t.toLongLong(&ok);
    return ok ? hz : 0;
}

QRect FreqEdit::charRect(int index) const
{
    // Where QLineEdit draws right-aligned text: the contents rect less the text margins and
    // QLineEdit's own 2 px horizontal margin.
    QStyleOptionFrame opt;
    initStyleOption(&opt);
    QRect r = style()->subElementRect(QStyle::SE_LineEditContents, &opt, this);
    r = r.marginsRemoved(textMargins());
    r.adjust(2, 0, -2, 0);
    const QFontMetrics fm(font());
    const QString t = text();
    const int x0 = r.right() + 1 - fm.horizontalAdvance(t);
    const int x = x0 + fm.horizontalAdvance(t.left(index));
    const int w = fm.horizontalAdvance(t.at(index));
    return QRect(x, r.top(), w, r.height());
}

int FreqEdit::indexAt(const QPoint &p) const
{
    const QString t = text();
    const std::string s = t.toStdString();
    for (int i = 0; i < t.size(); ++i)
        if (freqdigits::placeAt(s, i) > 0 && charRect(i).adjusted(0, -100, 0, 100).contains(p))
            return i;
    return -1;
}

void FreqEdit::setHover(int index)
{
    if (index == m_hover)
        return;
    m_hover = index;
    m_wheelAccum = 0;
    setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::IBeamCursor);
    update();
}

void FreqEdit::tune(int index, int steps)
{
    const qint64 place = freqdigits::placeAt(text().toStdString(), index);
    const qint64 hz = shownHz();
    if (place <= 0 || hz <= 0 || steps == 0)
        return;
    const qint64 nhz = freqdigits::step(hz, place, steps);
    if (nhz == hz)
        return;
    const int fromRight = text().size() - index; // keep the mouse on the same digit if a digit is added/lost
    setText(format(nhz));
    m_holdUntilMs = QDateTime::currentMSecsSinceEpoch() + kHoldMs;
    const int ni = text().size() - fromRight;
    m_hover = (ni >= 0 && freqdigits::placeAt(text().toStdString(), ni) > 0) ? ni : -1;
    update();
    emit tuneRequested(nhz);
}

void FreqEdit::paintEvent(QPaintEvent *e)
{
    QLineEdit::paintEvent(e);
    if (!digitMode() || m_hover < 0 || m_hover >= text().size())
        return;
    QPainter p(this);
    const QRect r = charRect(m_hover).adjusted(-1, 1, 1, -1);
    QColor c = palette().color(QPalette::Highlight);
    c.setAlpha(110);
    p.fillRect(r, c);
    // a small mark in the half the click would act on is not needed: up = top, down = bottom
    p.setPen(palette().color(QPalette::HighlightedText));
    p.drawLine(r.left() + 1, r.center().y(), r.right() - 1, r.center().y());
}

void FreqEdit::mouseMoveEvent(QMouseEvent *e)
{
    if (digitMode())
        setHover(indexAt(e->position().toPoint()));
    else
        setHover(-1);
    QLineEdit::mouseMoveEvent(e);
}

void FreqEdit::mousePressEvent(QMouseEvent *e)
{
    if (digitMode() && e->button() == Qt::LeftButton)
    {
        const int i = indexAt(e->position().toPoint());
        if (i >= 0)
        {
            tune(i, e->position().y() < charRect(i).center().y() ? 1 : -1);
            e->accept();
            return; // no focus: stays in digit mode
        }
    }
    QLineEdit::mousePressEvent(e);
}

void FreqEdit::mouseDoubleClickEvent(QMouseEvent *e)
{
    // The second click of a fast double click on a digit is another step, not a word selection.
    if (digitMode() && e->button() == Qt::LeftButton && indexAt(e->position().toPoint()) >= 0)
    {
        mousePressEvent(e);
        return;
    }
    QLineEdit::mouseDoubleClickEvent(e);
}

void FreqEdit::wheelEvent(QWheelEvent *e)
{
    if (!digitMode())
    {
        QLineEdit::wheelEvent(e);
        return;
    }
    const int i = indexAt(e->position().toPoint());
    if (i < 0)
    {
        e->ignore();
        return;
    }
    m_wheelAccum += e->angleDelta().y();
    const int steps = m_wheelAccum / 120; // one notch = one step; touchpads add up
    m_wheelAccum -= steps * 120;
    tune(i, steps);
    e->accept();
}

void FreqEdit::leaveEvent(QEvent *e)
{
    setHover(-1);
    QLineEdit::leaveEvent(e);
}

void FreqEdit::focusInEvent(QFocusEvent *e)
{
    setHover(-1);
    m_holdUntilMs = 0;
    QLineEdit::focusInEvent(e);
}
