#pragma once

// PSK tab: mode, frequency, AFC, squelch and the waterfall on top; decoded
// text in the middle (with the optional multi-signal list under it); macros
// and transmit box at the bottom.

#include "ModemPanel.h"
#include "PskEngine.h"

class SquelchControl;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QProgressBar;
class QSplitter;
class QTableWidget;

class PskPanel : public ModemPanel
{
    Q_OBJECT

public:
    PskPanel(PskEngine *engine, QSettings *settings, QWidget *parent = nullptr);
    // A line of its own in the receive pane, in amber (RSID notices).
    void addNote(const QString &text);
    void saveState() override;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void syncSettings() override;
    double markerBandwidth() const override;

private slots:
    void syncStats();
    void appendText(const QString &text);
    void showChannels(const QVector<PskChannelInfo> &channels);

private:
    QWidget *buildTop();
    // A multi-signal row: decode that station above, with what the row copied so far.
    void decodeRow(int row);
    // A call sign picked in the list: to the QSO log (lookup) and the Call box.
    void pickCall(const QString &call);

    PskEngine *m_psk;
    QPushButton *m_decode = nullptr;
    QComboBox *m_speed = nullptr;
    QDoubleSpinBox *m_freq = nullptr;
    QCheckBox *m_afc = nullptr;
    QCheckBox *m_autoSpeed = nullptr;
    SquelchControl *m_squelch = nullptr;
    QProgressBar *m_qualityBar = nullptr;
    QLabel *m_level = nullptr;
    QPlainTextEdit *m_rx = nullptr;
    QCheckBox *m_multi = nullptr;
    QTableWidget *m_table = nullptr;
    QSplitter *m_rxSplit = nullptr;
};
