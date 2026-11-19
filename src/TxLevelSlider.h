// TX level control on a dB scale (GitHub issue #2): a slider from 0 dB (full level) down to
// -40 dB in 0.5 dB steps, with the value shown next to it. The engines still store a linear
// gain 0..1 (engine/txlevel.h converts), so saved levels carry over unchanged.
#pragma once

#include <QWidget>

class QLabel;
class QSlider;

class TxLevelSlider : public QWidget
{
    Q_OBJECT
public:
    explicit TxLevelSlider(QWidget *parent = nullptr);

    // Shows a linear level (0..1) without emitting levelChanged.
    void setLevel(double level);
    double level() const;
    void setSliderToolTip(const QString &text);

signals:
    // Linear gain 0..1, only when the user moves the slider.
    void levelChanged(double level);

private:
    void updateLabel();

    QSlider *m_slider = nullptr;
    QLabel *m_value = nullptr;
};
