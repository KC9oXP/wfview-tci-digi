#include "FreqEdit.h"

#include "freqdigits.h"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QWheelEvent>
#include <cstdlib>

namespace
{
constexpr int kHoldMs = 700; // ignore the radio's reports this long after a wheel/click step
}

FreqEdit::FreqEdit(QWidget *parent)
    : QLineEdit(parent)
{
    setMouseTracking(true);
    setAlignment(Qt::AlignRight);
    // A click must not give the box the keyboard (that put a text cursor in and stopped the
    // digit stepping). Tab still reaches it for typing.
    setFocusPolicy(Qt::TabFocus);
    m_pendingTimer.setSingleShot(true);
    connect(&m_pendingTimer,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (m_lastHz > 0)
                    showHz(m_lastHz);
            });
    setContextMenuPolicy(Qt::DefaultContextMenu);
}

QString FreqEdit::format(qint64 hz)
{
    return QString::fromStdString(freqdigits::format(hz));
}

void FreqEdit::showHz(qint64 hz)
{
    if (hz > 0)
        m_lastHz = hz;
    if (hasFocus() || hz <= 0)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_holdUntilMs)
    {
        // Just stepped: the radio's report may lag behind. Show the newest one once the
        // hold is over (so a change made elsewhere is not missed).
        if (!m_pendingTimer.isActive())
            m_pendingTimer.start(int(m_holdUntilMs - now) + 10);
        return;
    }
    const QString t = format(hz);
    if (t != text())
        setText(t);
}

void FreqEdit::startTyping()
{
    if (!isEnabled())
        return;
    setHover(-1);
    setFocus(Qt::OtherFocusReason);
    selectAll();
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

int FreqEdit::nearestDigit(const QPoint &p) const
{
    // A click on a dot or just beside the digits goes to the closest digit.
    const int exact = indexAt(p);
    if (exact >= 0)
        return exact;
    const QString t = text();
    const std::string s = t.toStdString();
    int best = -1, bestD = 1 << 30;
    for (int i = 0; i < t.size(); ++i)
        if (freqdigits::placeAt(s, i) > 0)
        {
            const int d = std::abs(charRect(i).center().x() - p.x());
            if (d < bestD)
            {
                bestD = d;
                best = i;
            }
        }
    return best;
}

void FreqEdit::setHover(int index)
{
    if (index == m_hover)
        return;
    m_hover = index;
    m_wheelAccum = 0;
    setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
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
    setText(format(nhz)); // fixed width: the mouse stays on the same digit
    m_holdUntilMs = QDateTime::currentMSecsSinceEpoch() + kHoldMs;
    update();
    emit tuneRequested(nhz);
}

void FreqEdit::paintEvent(QPaintEvent *e)
{
    if (hasFocus())
    {
        QLineEdit::paintEvent(e); // typing: a plain text box
        return;
    }
    // Not typing: draw the frame and the digits ourselves so the leading zeros can be
    // dimmer than the real digits.
    const QString t = text();
    const QColor full = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::Text);
    QPainter p(this);
    {
        QStyleOptionFrame opt;
        initStyleOption(&opt);
        style()->drawPrimitive(QStyle::PE_PanelLineEdit, &opt, &p, this);
    }
    p.setFont(font());
    const int dim = freqdigits::leadingZeros(t.toStdString());
    QColor faint = full;
    faint.setAlphaF(0.35);
    for (int i = 0; i < t.size(); ++i)
    {
        const QRect r = charRect(i);
        p.setPen(i < dim ? faint : full);
        p.drawText(r, Qt::AlignCenter, t.at(i));
    }
    if (!digitMode() || m_hover < 0 || m_hover >= t.size())
        return;
    const QRect r = charRect(m_hover).adjusted(-1, 1, 1, -1);
    QColor c = palette().color(QPalette::Highlight);
    c.setAlpha(110);
    p.fillRect(r, c);
    p.setPen(palette().color(QPalette::HighlightedText));
    p.drawLine(r.left() + 1, r.center().y(), r.right() - 1, r.center().y());
}

void FreqEdit::mouseMoveEvent(QMouseEvent *e)
{
    if (digitMode())
    {
        setHover(nearestDigit(e->position().toPoint()));
        e->accept();
        return; // no text selection by dragging
    }
    setHover(-1);
    QLineEdit::mouseMoveEvent(e);
}

void FreqEdit::mousePressEvent(QMouseEvent *e)
{
    if (hasFocus())
    {
        QLineEdit::mousePressEvent(e); // typing: place the cursor as usual
        return;
    }
    if (e->button() == Qt::LeftButton && digitMode())
    {
        const int i = nearestDigit(e->position().toPoint());
        if (i >= 0)
            tune(i, e->position().y() < charRect(i).center().y() ? 1 : -1);
    }
    e->accept(); // never start typing from a click
}

void FreqEdit::mouseReleaseEvent(QMouseEvent *e)
{
    if (hasFocus())
        QLineEdit::mouseReleaseEvent(e);
    else
        e->accept();
}

void FreqEdit::mouseDoubleClickEvent(QMouseEvent *e)
{
    // The second click of a fast double click is another step, not a word selection.
    if (!hasFocus())
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
    const int i = nearestDigit(e->position().toPoint());
    if (i < 0)
    {
        e->ignore();
        return;
    }
    if (i != m_hover)
        setHover(i);
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
    update();
}

void FreqEdit::focusOutEvent(QFocusEvent *e)
{
    QLineEdit::focusOutEvent(e);
    // Left without Enter: show the radio's frequency again.
    if (m_lastHz > 0)
        setText(format(m_lastHz));
    update();
}

void FreqEdit::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape)
    {
        if (m_lastHz > 0)
            setText(format(m_lastHz));
        clearFocus();
        emit typingCancelled();
        e->accept();
        return;
    }
    QLineEdit::keyPressEvent(e);
}

void FreqEdit::contextMenuEvent(QContextMenuEvent *e)
{
    if (hasFocus())
    {
        QLineEdit::contextMenuEvent(e);
        return;
    }
    QMenu menu(this);
    QAction *type = menu.addAction(tr("Type a frequency..."));
    type->setEnabled(isEnabled());
    QAction *copy = menu.addAction(tr("Copy"));
    QAction *a = menu.exec(e->globalPos());
    if (a == type)
        startTyping();
    else if (a == copy && shownHz() > 0)
        QGuiApplication::clipboard()->setText(QString::number(shownHz())); // 14074000, no dots or zeros
}
