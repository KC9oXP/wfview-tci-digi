#pragma once

// PSK tab: mode, frequency, AFC, squelch and the waterfall on top; decoded
// text in the middle; macros and transmit box at the bottom.

#include "ModemPanel.h"

class PskEngine;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QProgressBar;

class PskPanel : public ModemPanel
{
    Q_OBJECT

public:
    PskPanel(PskEngine *engine, QSettings *settings, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void syncSettings() override;
    double markerBandwidth() const override;

private slots:
    void syncStats();
    void appendText(const QString &text);

private:
    QWidget *buildTop();

    PskEngine *m_psk;
    QPushButton *m_decode = nullptr;
    QComboBox *m_speed = nullptr;
    QDoubleSpinBox *m_freq = nullptr;
    QCheckBox *m_afc = nullptr;
    QSlider *m_squelch = nullptr;
    QLabel *m_squelchValue = nullptr;
    QProgressBar *m_qualityBar = nullptr;
    QLabel *m_level = nullptr;
    QPlainTextEdit *m_rx = nullptr;
};
