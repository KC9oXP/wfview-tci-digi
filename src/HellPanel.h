#pragma once

// Feld Hell tab: frequency, receive speed trim, zoom, contrast and the
// waterfall on top; the painted receive display in the middle; macros and
// transmit box at the bottom.

#include "ModemPanel.h"

class HellEngine;
class HellWidget;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;

class HellPanel : public ModemPanel
{
    Q_OBJECT

public:
    HellPanel(HellEngine *engine, QSettings *settings, QWidget *parent = nullptr);
    void saveState() override; // also auto-saves the received picture
    // Saves the received picture as PNG in the logs folder: everything kept,
    // or (automatic) what came in since the last automatic save. Returns the path.
    QString savePicture(bool sinceLastAuto);

protected:
    void syncSettings() override;
    double markerBandwidth() const override
    {
        return 250.0;
    }

private:
    QWidget *buildTop();
    void syncAlign();

    HellEngine *m_hell;
    QPushButton *m_decode = nullptr;
    QDoubleSpinBox *m_freq = nullptr;
    QDoubleSpinBox *m_trim = nullptr;
    QComboBox *m_zoom = nullptr;
    QSlider *m_contrast = nullptr;
    QLabel *m_level = nullptr;
    HellWidget *m_rx = nullptr;
    QPlainTextEdit *m_echo = nullptr;
    QCheckBox *m_autoAlign = nullptr;
    QSpinBox *m_shift = nullptr;
    QLabel *m_shiftInfo = nullptr;
    QCheckBox *m_autoSave = nullptr;
    qint64 m_savedTo = 0; // last column auto-saved
    bool m_wasDecoding = false;
};
