#pragma once

// Feld Hell receive display. Each received column is drawn twice, one copy
// above the other, so the text stays readable when it drifts up or down.
// Columns run left to right in strips like lines of text; when the window is
// full it scrolls up a strip.

#include <QImage>
#include <QVector>
#include <QWidget>

class HellWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HellWidget(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    int zoom() const
    {
        return m_zoom;
    }
    double gain() const
    {
        return m_gain;
    }

public slots:
    void addColumns(const QVector<float> &cols);
    void setZoom(int z); // pixels per half-pixel row, 1..4
    void setGain(double g); // contrast, 0.5..4
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    int stripHeight() const;
    int colWidth() const
    {
        return 2 * m_zoom;
    }
    void newStrip();
    void paintColumns(const QVector<float> &cols);

    QImage m_img;
    int m_zoom = 2;
    double m_gain = 1.5;
    int m_x = 0; // next column position in the current strip
    int m_strip = 0; // current strip index
    QVector<float> m_pending; // received while hidden, painted when shown
};
