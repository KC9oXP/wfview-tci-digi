#pragma once

// The Squelch row piece shared by the CW, RTTY, PSK, Olivia and MT63 tabs:
// "Squelch" label, slider, value ("off" at 0), a dB box and the squelch meter.
// The slider works on the mode's own scale, or in dB with the box ticked;
// each scale keeps its own setting. On CW and MT63 the mode's own scale is
// already dB: the box is shown ticked and cannot be changed.

#include "SquelchMeter.h"

#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QSlider;

class SquelchControl : public QWidget
{
    Q_OBJECT

public:
    struct Scale
    {
        double max = 1.0;
        double step = 0.01;
        int decimals = 2;
        QString unit; // shown after the value, e.g. " dB"
        QString tip; // slider tooltip
    };

    // dbOnly: the mode's own scale is dB (nativeScale is then not used).
    SquelchControl(const Scale &nativeScale, const Scale &dbScale, bool dbOnly, QWidget *parent = nullptr);

    // Shows a setting (no signals).
    void setSetting(double value, bool inDb);
    // The decoder's level now (in the scale in use) and whether it is printing;
    // active false while not decoding.
    void setReading(double level, bool open, bool active);

    bool inDb() const
    {
        return m_inDb;
    }
    double value() const;
    SquelchMeter *meter() const
    {
        return m_meter;
    }

signals:
    // The slider moved: the new setting on the scale in use.
    void valueChanged(double value);
    void inDbToggled(bool on);

private:
    const Scale &scale() const
    {
        return m_inDb ? m_db : m_native;
    }
    void applyScale();
    void showValue();
    QString text(double v) const;

    Scale m_native, m_db;
    bool m_dbOnly = false;
    bool m_inDb = false;
    double m_level = 0.0;
    bool m_open = false;
    bool m_active = false;
    QSlider *m_slider = nullptr;
    QLabel *m_value = nullptr;
    QCheckBox *m_dbBox = nullptr;
    SquelchMeter *m_meter = nullptr;
};
