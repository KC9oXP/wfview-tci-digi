#pragma once

// Squelch meter shown next to each tab's Squelch slider: the level the
// decoder measures now and a marker at the squelch setting it must reach.
// Two looks, picked for every tab at once in Options > Squelch meter:
//   Bar:       light green bar (bright while the squelch is open), white tick
//              at the setting;
//   Magic eye: a tuning-eye whose dark wedge closes as the level rises, with
//              ticks on the rim where its edges stand at the setting; it glows
//              brighter while open.

#include <QWidget>

class SquelchMeter : public QWidget
{
    Q_OBJECT

public:
    enum class Style
    {
        Bar,
        Eye
    };

    explicit SquelchMeter(QWidget *parent = nullptr);
    ~SquelchMeter() override;

    // Full scale of the meter (the slider's maximum); level and threshold are
    // in the same units.
    void setRange(double max);
    // threshold 0 = squelch off (no marker). active false: not decoding (empty, dim).
    void setReading(double level, double threshold, bool open, bool active);

    static Style style();
    // Changes every meter; MainWindow calls it from the Options menu.
    static void setStyleAll(Style s);
    static Style styleFromString(const QString &s);
    static QString styleToString(Style s);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void paintBar(class QPainter &p);
    void paintEye(class QPainter &p);
    double fraction(double v) const;

    double m_max = 1.0;
    double m_level = 0.0;
    double m_threshold = 0.0;
    bool m_open = false;
    bool m_active = false;
};
