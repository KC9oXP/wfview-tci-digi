// The receiver frequency box, always 9 digits with leading zeros ("001.810.000"), so any
// digit up to 100 MHz can be stepped. The digit under the mouse is highlighted; the mouse
// wheel steps it, and a click in the upper half of a digit steps it up, in the lower half
// down. Clicks never start typing (no text cursor to get in the way). To type a frequency:
// right-click > Type a frequency..., or Tab into the box; Enter tunes, Esc cancels.
#pragma once

#include <QLineEdit>
#include <QTimer>

class FreqEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit FreqEdit(QWidget *parent = nullptr);

    // Shows the radio's frequency, unless the user is typing or has just tuned
    // (the radio's own reports lag behind a fast wheel).
    void showHz(qint64 hz);
    static QString format(qint64 hz);
    // Puts the box in typing mode (text selected, cursor in it).
    void startTyping();

signals:
    void tuneRequested(qint64 hz);
    void typingCancelled();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    bool digitMode() const;
    int indexAt(const QPoint &p) const;
    int nearestDigit(const QPoint &p) const;
    QRect charRect(int index) const;
    qint64 shownHz() const;
    void tune(int index, int steps);
    void setHover(int index);

    int m_hover = -1;
    int m_wheelAccum = 0;
    qint64 m_holdUntilMs = 0;
    qint64 m_lastHz = 0; // what the radio last reported, put back on Esc
    QTimer m_pendingTimer; // shows m_lastHz once a step's hold is over
};
