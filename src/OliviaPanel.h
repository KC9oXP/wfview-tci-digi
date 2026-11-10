#pragma once

// Olivia / Contestia tab: mode, frequency, squelch and the waterfall on top;
// decoded text in the middle; macros and transmit box at the bottom.

#include "ModemPanel.h"

class OliviaEngine;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;

class OliviaPanel : public ModemPanel
{
    Q_OBJECT

public:
    OliviaPanel(OliviaEngine *engine, QSettings *settings, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void syncSettings() override;
    double markerBandwidth() const override;

private slots:
    void syncStats();

private:
    QWidget *buildTop();

    OliviaEngine *m_ol;
    QPushButton *m_decode = nullptr;
    QComboBox *m_mode = nullptr;
    QDoubleSpinBox *m_freq = nullptr;
    QSlider *m_squelch = nullptr;
    QLabel *m_squelchValue = nullptr;
    QLabel *m_stats = nullptr;
    QPlainTextEdit *m_rx = nullptr;
};
