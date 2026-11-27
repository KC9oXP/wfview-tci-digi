#pragma once

// Picture display for SSTV and WEFAX.
//   FitWindow: the whole picture, aspect kept (SSTV).
//   FitWidth: scaled to the width, as tall as it needs (WEFAX, in a scroll area).
// columnShift rotates every row left (WEFAX alignment). Clicks report the
// picture coordinate.
// Edit boxes (SSTV transmit preview, FitWindow only): outlines with corner handles over the picture,
// moved by dragging inside and resized by dragging a corner. Kept as fractions of the picture.

#include <QImage>
#include <QRectF>
#include <QVector>
#include <QWidget>

class ImageView : public QWidget
{
    Q_OBJECT

public:
    enum class Fit
    {
        Window,
        Width
    };
    explicit ImageView(Fit fit, QWidget *parent = nullptr);

    void setImage(const QImage &img, int rowsShown = -1);
    const QImage &image() const
    {
        return m_img;
    }
    void setColumnShift(int px);
    int columnShift() const
    {
        return m_shift;
    }
    void setPlaceholder(const QString &text);
    // A bright line across the picture at this fraction of its height (0..1), e.g. the
    // line being transmitted; negative hides it. Kept across setImage().
    void setMarkerLine(double fraction);
    // The picture as shown (shift applied, cropped to rowsShown).
    QImage shownImage() const;

    QSize sizeHint() const override;
    bool hasHeightForWidth() const override
    {
        return m_fit == Fit::Width;
    }
    int heightForWidth(int w) const override;

    // Boxes to edit, as fractions of the picture (0..1); an empty rect leaves that number out.
    // The outlines show only while editing is allowed (not while the picture is being sent).
    void setEditBoxes(const QVector<QRectF> &boxes);
    void setBoxesEditable(bool on);
    bool boxesEditable() const
    {
        return m_boxEdit;
    }
    // Where box i is on screen (widget pixels), for tests; empty if not shown.
    QRect boxOnScreen(int i) const;

signals:
    void clicked(QPoint imagePos);
    // While dragging: the box's new place (fractions). boxReleased when the mouse button comes up.
    void boxMoved(int index, QRectF fraction);
    void boxReleased(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRect target() const;
    bool showBoxes() const;
    // Top-most box under pos and what part of it; -1 if none.
    int boxAt(const QPointF &pos, int *grip) const;

    Fit m_fit;
    QImage m_img;
    int m_rows = -1;
    int m_shift = 0;
    QString m_placeholder;
    double m_marker = -1.0;
    QVector<QRectF> m_boxes;
    bool m_boxEdit = false;
    int m_dragBox = -1;
    int m_dragGrip = 0;
    QPointF m_dragFrom;
    QRectF m_dragStart;
};
