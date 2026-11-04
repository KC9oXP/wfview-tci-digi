#pragma once

// Audio waterfall, 0..3 kHz, newest line at the top, with a frequency scale
// and the modem's carrier marker. Click to tune (emits clicked), mouse wheel
// nudges 1 Hz (emits nudged), hover shows the frequency.

#include <QImage>
#include <QVector>
#include <QWidget>

class WaterfallWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WaterfallWidget(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    double minHz() const
    {
        return m_minHz;
    }
    double maxHz() const
    {
        return m_maxHz;
    }

public slots:
    void addLine(const QVector<float> &db, double hzPerBin);
    // Carrier and the signal width drawn around it.
    void setMarker(double hz, double bandwidthHz);
    void setTransmitting(bool on);
    void clearImage();

signals:
    void clicked(double hz);
    void nudged(double hz);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    int scaleHeight() const;
    double xToHz(double x) const;
    double hzToX(double hz) const;
    QRgb colour(double v) const; // v 0..1

    QImage m_img;
    double m_minHz = 0.0;
    double m_maxHz = 3000.0;
    double m_marker = 1000.0;
    double m_bw = 31.25;
    bool m_tx = false;
    double m_floor = -100.0; // slowly tracked noise floor, dB
    bool m_floorSet = false;
    int m_hoverX = -1;
    QVector<QRgb> m_palette;
};
