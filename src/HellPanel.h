#pragma once

// Feld Hell tab: frequency, receive speed trim, zoom, contrast and the
// waterfall on top; the painted receive display in the middle; macros and
// transmit box at the bottom.

#include "ModemPanel.h"

class HellEngine;
class HellWidget;
class QComboBox;
class QDoubleSpinBox;

class HellPanel : public ModemPanel
{
    Q_OBJECT

public:
    HellPanel(HellEngine *engine, QSettings *settings, QWidget *parent = nullptr);
    void saveState() override;

protected:
    void syncSettings() override;
    double markerBandwidth() const override
    {
        return 250.0;
    }

private:
    QWidget *buildTop();

    HellEngine *m_hell;
    QPushButton *m_decode = nullptr;
    QDoubleSpinBox *m_freq = nullptr;
    QDoubleSpinBox *m_trim = nullptr;
    QComboBox *m_zoom = nullptr;
    QSlider *m_contrast = nullptr;
    QLabel *m_level = nullptr;
    HellWidget *m_rx = nullptr;
    QPlainTextEdit *m_echo = nullptr;
};
