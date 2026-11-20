#pragma once

// Picture display for SSTV and WEFAX.
//   FitWindow: the whole picture, aspect kept (SSTV).
//   FitWidth: scaled to the width, as tall as it needs (WEFAX, in a scroll area).
// columnShift rotates every row left (WEFAX alignment). Clicks report the
// picture coordinate.

#include <QImage>
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

signals:
    void clicked(QPoint imagePos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRect target() const;

    Fit m_fit;
    QImage m_img;
    int m_rows = -1;
    int m_shift = 0;
    QString m_placeholder;
    double m_marker = -1.0;
};
