#pragma once

// MT63 tab: mode, frequency, squelch and the waterfall on top; decoded text in
// the middle; macros and transmit box at the bottom.

#include "ModemPanel.h"

class Mt63Engine;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;

class Mt63Panel : public ModemPanel
{
    Q_OBJECT

public:
    Mt63Panel(Mt63Engine *engine, QSettings *settings, QWidget *parent = nullptr);
    // A line of its own in the receive pane, in amber (RSID notices).
    void addNote(const QString &text);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void syncSettings() override;
    double markerBandwidth() const override;

private slots:
    void syncStats();

private:
    QWidget *buildTop();

    Mt63Engine *m_mt;
    QPushButton *m_decode = nullptr;
    QComboBox *m_mode = nullptr;
    QDoubleSpinBox *m_freq = nullptr;
    QSlider *m_squelch = nullptr;
    QLabel *m_squelchValue = nullptr;
    QLabel *m_stats = nullptr;
    QPlainTextEdit *m_rx = nullptr;
};
