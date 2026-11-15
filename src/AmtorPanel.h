#pragma once

// AMTOR tab (FEC / mode B): Decode, centre frequency, Reverse and the
// waterfall on top; received text in the middle; macros and the transmit
// box at the bottom.

#include "ModemPanel.h"

class AmtorEngine;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class AmtorPanel : public ModemPanel
{
    Q_OBJECT

public:
    AmtorPanel(AmtorEngine *engine, QSettings *settings, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void syncSettings() override;
    double markerBandwidth() const override
    {
        return 270.0;
    }

private slots:
    void syncStats();

private:
    QWidget *buildTop();

    AmtorEngine *m_am;
    QPushButton *m_decode = nullptr;
    QDoubleSpinBox *m_freq = nullptr;
    QCheckBox *m_reverse = nullptr;
    QLabel *m_stats = nullptr;
    QPlainTextEdit *m_rx = nullptr;
};
