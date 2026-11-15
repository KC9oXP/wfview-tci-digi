#pragma once

// Audio waterfall, 0..3 kHz, newest line at the top, with a frequency scale
// and the modem's carrier marker. Click to tune (emits clicked), mouse wheel
// nudges 1 Hz (emits nudged), hover shows the frequency.
//
// History: the last kHistRows lines are kept. Shift+wheel (or PgUp/PgDn)
// scrolls back in time; clicking still tunes. Esc, End or right-click > Live
// returns to the newest line.

#include <QImage>
#include <QStringList>
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
    // Thin labelled lines at these frequencies (e.g. SSTV 1200/1500/1900/2300 Hz).
    void setGuides(const QVector<double> &hz, const QStringList &labels = QStringList());
    void setTransmitting(bool on);
    void clearImage();
    void goLive();

public:
    static constexpr int kHistRows = 2000;
    int historyOffset() const
    {
        return m_offset;
    }

signals:
    void clicked(double hz);
    void nudged(double hz);
    void saveRequested(); // context menu: save a picture

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    int scaleHeight() const;
    double xToHz(double x) const;
    double hzToX(double hz) const;
    QRgb colour(double v) const; // v 0..1
    int visibleRows() const;
    void scrollBy(int rows);

    QImage m_img; // ring of kHistRows lines; m_head is the newest
    int m_head = 0;
    int m_count = 0;
    int m_offset = 0; // lines scrolled back from the newest
    QVector<qint64> m_times; // ms since epoch of each line
    double m_minHz = 0.0;
    double m_maxHz = 3000.0;
    double m_marker = 1000.0;
    double m_bw = 31.25;
    QVector<double> m_guides;
    QStringList m_guideLabels;
    bool m_tx = false;
    double m_floor = -100.0; // slowly tracked noise floor, dB
    bool m_floorSet = false;
    int m_hoverX = -1;
    QVector<QRgb> m_palette;
};
