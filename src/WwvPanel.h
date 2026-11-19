#pragma once

// WWV Time tab (receive only): lamps for what has been heard (ticks, tones,
// the 100 Hz code, a decoded time, a measured carrier), the UTC time and how
// far the PC clock is from it, and the calibration figures: audio clock error
// from the ticks, how far the tones read off, and the carrier's offset on the
// dial.

#include <QWidget>

#include <functional>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class WaterfallWidget;
class WwvEngine;

class WwvPanel : public QWidget
{
    Q_OBJECT

public:
    explicit WwvPanel(WwvEngine *engine, QWidget *parent = nullptr);
    WaterfallWidget *waterfall() const
    {
        return m_wf;
    }

    // Reads the SSTV and WEFAX Trim values (ppm) so this tab can show them.
    void setTrimSource(std::function<void(int &sstv, int &wefax)> source);

public slots:
    void addSpectrumLine(const QVector<float> &db, double hzPerBin);

signals:
    // "Copy to trim" or the Trim box here: set the SSTV and WEFAX trim to this.
    void trimPpmChosen(double ppm);

private:
    QLabel *makeLamp(const QString &text, const QString &tip);
    void setLamp(QLabel *lamp, bool on, const QString &text = QString());
    void sync();
    void syncStats();
    void updateClockLine();
    void syncTrim();

    WwvEngine *m_wwv;
    QPushButton *m_decode = nullptr;
    QLabel *m_lampTicks = nullptr, *m_lampTones = nullptr, *m_lampCode = nullptr, *m_lampTime = nullptr,
           *m_lampCarrier = nullptr, *m_station = nullptr;
    QLabel *m_time = nullptr, *m_date = nullptr, *m_flags = nullptr, *m_pc = nullptr;
    QLineEdit *m_grid = nullptr;
    QLabel *m_path = nullptr;
    QDoubleSpinBox *m_delay = nullptr;
    QPushButton *m_calibrate = nullptr, *m_setClock = nullptr;
    QLabel *m_clock = nullptr, *m_tone = nullptr, *m_carrier = nullptr, *m_symbols = nullptr, *m_message = nullptr;
    QPushButton *m_copyPpm = nullptr, *m_measure = nullptr;
    QSpinBox *m_trim = nullptr;
    QLabel *m_trimNote = nullptr;
    std::function<void(int &, int &)> m_trimSource;
    WaterfallWidget *m_wf = nullptr;
    QTimer *m_tick = nullptr;
};
