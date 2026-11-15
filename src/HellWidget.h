#pragma once

// Feld Hell receive display. Each received column is drawn twice, one copy
// above the other, so the text stays readable when it drifts up or down.
// Columns run left to right in strips like lines of text; when the window is
// full it scrolls up a strip.
//
// The received half-pixels are kept (about 30 minutes), so:
//   - column phase: the receiver starts its columns at an arbitrary moment,
//     which splits letters across the column edge (fldigi shows the same).
//     The display re-cuts the columns at a shift found automatically (where
//     the strokes break) plus a manual correction, for everything kept.
//   - history: mouse wheel / PgUp scrolls back by strips; a Live button
//     returns. New text does not move a scrolled-back view.
//   - render() draws any stretch of it for saving as a picture.

#include <QImage>
#include <QVector>
#include <QWidget>

class QPushButton;

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
    bool autoAlign() const
    {
        return m_autoAlign;
    }
    int manualShift() const
    {
        return m_manual;
    }
    // Shift in use, 0..13 half-pixels.
    int rowShift() const;
    bool live() const
    {
        return m_back == 0;
    }

    // Columns are numbered from the first ever received (absolute).
    qint64 firstColumn() const;
    qint64 endColumn() const; // one past the newest complete column
    // Columns [from, to) as strips of perStrip columns at this zoom and contrast.
    QImage render(qint64 from, qint64 to, int perStrip, int zoom) const;
    static constexpr int kKeepSeconds = 1800;

public slots:
    void addColumns(const QVector<float> &cols);
    void setZoom(int z); // pixels per half-pixel row, 1..4
    void setGain(double g); // contrast, 0.5..4
    void setAutoAlign(bool on);
    void setManualShift(int halfPixels); // -7..+7, added to the automatic shift
    void clear();
    void goLive();

signals:
    void alignmentChanged(); // rowShift() changed
    void liveChanged(bool live);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    int stripHeight() const;
    int colWidth() const
    {
        return 2 * m_zoom;
    }
    int columnsPerStrip() const;
    int visibleStrips() const;
    qint64 newestStrip() const;
    void scrollStrips(int n);
    void placeLive();
    void updateAutoShift();
    float value(qint64 col, int row) const; // aligned column, row 0 = bottom
    void drawColumn(QImage &img, int x, int top, qint64 col, int zoom) const;

    QVector<float> m_px; // received half-pixels
    qint64 m_dropped = 0; // half-pixels dropped from the front
    qint64 m_colBase = 0; // column that starts the first strip (set by Clear)
    int m_zoom = 2;
    double m_gain = 1.5;
    bool m_autoAlign = true;
    int m_autoShift = 0;
    int m_candidate = -1;
    int m_manual = 0;
    qint64 m_sinceAlign = 0;
    int m_back = 0; // strips scrolled back
    qint64 m_lastNewest = -1;
    QPushButton *m_live = nullptr;
    mutable QImage m_canvas;
    mutable bool m_dirty = true;
};
