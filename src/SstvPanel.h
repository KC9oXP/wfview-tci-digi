#pragma once

// SSTV tab.
//   receive: Decode, auto slant / trim, Start now (no VIS) and Stop, the
//   picture as it comes in, a strip of received pictures, a small waterfall
//   transmit: open or paste a picture, mode, picture text (a choice of CQ /
//   Answer / Report / Thanks / 73 / Test / Custom, each editable; {MYCALL}
//   {CALL} {RST} {GRID} {DATE} {TIME}), optional timestamp, preview,
//   Send / Abort, level and progress

#include <QImage>
#include <QWidget>

class ImageView;
class QButtonGroup;
class QDateTime;
class QTimer;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSettings;
class QSlider;
class TxLevelSlider;
class QSpinBox;
class SstvEngine;
class TxToolButtons;
class WaterfallWidget;

class SstvPanel : public QWidget
{
    Q_OBJECT

public:
    SstvPanel(SstvEngine *engine, QSettings *settings, QWidget *parent = nullptr);
    void saveState();
    WaterfallWidget *waterfall() const
    {
        return m_wf;
    }

public slots:
    void setRadioState(bool ready, bool canTransmit, const QString &mode);
    void addSpectrumLine(const QVector<float> &db, double hzPerBin);

private:
    QWidget *buildRx();
    QWidget *buildTx();
    void sync();
    void syncTx(bool on);
    void loadTxImage(const QImage &img);
    void updatePreview();
    QImage composeTx() const;
    void showMsgChoice(int id);
    QString fillText(QString text, const QDateTime &utc) const;
    void addThumb(const QString &path, const QImage &img);
    void updateMarkers();

    SstvEngine *m_sstv;
    QSettings *m_settings;
    bool m_ready = false, m_canTx = false;

    QPushButton *m_decode = nullptr;
    QCheckBox *m_autoSlant = nullptr;
    QSpinBox *m_trim = nullptr;
    QComboBox *m_manualMode = nullptr;
    QCheckBox *m_autoStart = nullptr;
    QSpinBox *m_offset = nullptr;
    QCheckBox *m_autoOffset = nullptr;
    QLabel *m_offsetInfo = nullptr;
    QPushButton *m_startNow = nullptr;
    QPushButton *m_stop = nullptr;
    QPushButton *m_save = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_level = nullptr;
    ImageView *m_view = nullptr;
    QListWidget *m_gallery = nullptr;
    WaterfallWidget *m_wf = nullptr;

    QImage m_txSource;
    QComboBox *m_txMode = nullptr;
    QButtonGroup *m_msgGroup = nullptr;
    QLineEdit *m_overlay = nullptr;
    QLineEdit *m_rst = nullptr;
    QCheckBox *m_stamp = nullptr;
    QComboBox *m_stampZone = nullptr;
    QComboBox *m_stampPos = nullptr;
    QTimer *m_stampTimer = nullptr;
    QLineEdit *m_call = nullptr;
    ImageView *m_preview = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_abort = nullptr;
    TxLevelSlider *m_txLevel = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_txWarn = nullptr;
    QCheckBox *m_cwIdAfter = nullptr;
    TxToolButtons *m_tools = nullptr;
};
