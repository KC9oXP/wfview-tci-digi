#pragma once

// WSPR tab: Decode, the slot clock, the beacon (call / grid / power, TX %,
// offset), the 200 Hz window with busy offsets and free suggestions, band
// selection and hopping, WSPRnet upload, and the table of spots.

#include "WsprEngine.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSettings;
class QSpinBox;
class QTableWidget;
class TxLevelSlider;
class WsprNet;
class WsprWindowView;

class WsprPanel : public QWidget
{
    Q_OBJECT

public:
    WsprPanel(WsprEngine *engine, WsprNet *net, QSettings *settings, QWidget *parent = nullptr);
    void saveState();

public slots:
    void setRadioState(bool ready, bool canTransmit, const QString &mode);

signals:
    // Go to a band's WSPR dial (USB).
    void bandRequested(qint64 dialHz, QString why);

private slots:
    void syncSettings();
    void syncSlot();
    void syncFree();
    void addSlot(const WsprSlot &s);
    void syncUpload();

private:
    QWidget *buildTop();
    void editHopBands();
    void populateBands();

    WsprEngine *m_w;
    WsprNet *m_net;
    QSettings *m_settings;

    QPushButton *m_decode = nullptr;
    QComboBox *m_depth = nullptr;
    QLabel *m_clock = nullptr;
    QProgressBar *m_slotBar = nullptr;

    QPushButton *m_txEnable = nullptr;
    QSpinBox *m_pct = nullptr;
    QPushButton *m_txNext = nullptr;
    QLineEdit *m_call = nullptr;
    QLineEdit *m_grid = nullptr;
    QComboBox *m_dbm = nullptr;
    QCheckBox *m_autoPower = nullptr;
    class QDoubleSpinBox *m_loss = nullptr;
    QLabel *m_powerNote = nullptr;
    TxLevelSlider *m_level = nullptr;
    QLabel *m_msgs = nullptr;

    QSpinBox *m_offset = nullptr;
    QCheckBox *m_autoFree = nullptr;
    QPushButton *m_pick[3] = {nullptr, nullptr, nullptr};
    QLabel *m_busy = nullptr;
    WsprWindowView *m_view = nullptr;

    QComboBox *m_band = nullptr;
    QPushButton *m_go = nullptr;
    QComboBox *m_hop = nullptr;
    QPushButton *m_hopBands = nullptr;

    QCheckBox *m_upload = nullptr;
    QLabel *m_uploadState = nullptr;
    QLabel *m_clockHint = nullptr;
    QLabel *m_status = nullptr;
    QTableWidget *m_table = nullptr;

    bool m_ready = false;
    bool m_canTx = false;
};
