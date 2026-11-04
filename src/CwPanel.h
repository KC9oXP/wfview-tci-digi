#pragma once

// CW tab: decoder controls, decode pane, (optional) multi-signal list and the
// transmit area (audio CW, see CwTxEngine).

#include "CwDecoder.h"

#include <QWidget>

class CwTxEngine;
class ModemTxBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QSplitter;
class QTableWidget;

class CwPanel : public QWidget
{
    Q_OBJECT

public:
    CwPanel(CwDecoder *decoder, CwTxEngine *tx, QSettings *settings, QWidget *parent = nullptr);
    void saveState();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

public slots:
    void setRadioState(bool ready, bool canTransmit, const QString &mode);

private slots:
    void syncFromDecoder();
    void appendText(const QString &text);
    void showChannels(const QVector<CwChannelInfo> &channels);
    void syncTx();

private:
    void loadState();

    CwDecoder *m_dec;
    CwTxEngine *m_tx;
    QSettings *m_settings;

    QPushButton *m_decode = nullptr;
    QComboBox *m_engine = nullptr;
    QComboBox *m_pitchMode = nullptr;
    QSpinBox *m_pitch = nullptr;
    QCheckBox *m_fixedSpeed = nullptr;
    QSpinBox *m_speed = nullptr;
    QCheckBox *m_squelch = nullptr;
    QCheckBox *m_multi = nullptr;
    QLabel *m_status = nullptr;
    QPlainTextEdit *m_text = nullptr;
    QTableWidget *m_table = nullptr;
    QSplitter *m_split = nullptr;
    QSplitter *m_outer = nullptr; // receive | transmit
    QSpinBox *m_txWpm = nullptr;
    QSpinBox *m_txPitch = nullptr;
    QLabel *m_txStatus = nullptr;
    ModemTxBox *m_txBox = nullptr;
};
