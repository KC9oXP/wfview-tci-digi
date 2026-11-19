// The receiver frequency box. Besides typing a frequency and pressing Enter, the digit
// under the mouse is highlighted; the mouse wheel steps that digit, and a click in the
// upper half of a digit steps it up, in the lower half down. Clicking beside the digits
// (or pressing Tab into the box) starts typing as before.
#pragma once

#include <QLineEdit>

class FreqEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit FreqEdit(QWidget *parent = nullptr);

    // Shows the radio's frequency, unless the user is typing or has just tuned
    // (the radio's own reports lag behind a fast wheel).
    void showHz(qint64 hz);
    static QString format(qint64 hz);

signals:
    void tuneRequested(qint64 hz);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;

private:
    bool digitMode() const;
    int indexAt(const QPoint &p) const;
    QRect charRect(int index) const;
    qint64 shownHz() const;
    void tune(int index, int steps);
    void setHover(int index);

    int m_hover = -1;
    int m_wheelAccum = 0;
    qint64 m_holdUntilMs = 0;
};
