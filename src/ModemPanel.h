#pragma once

// Common layout for the PSK and Feld Hell tabs:
//   top (mode controls + waterfall) / receive pane / macros + transmit box,
// in a vertical splitter. A subclass builds the top and the receive pane and
// then calls finishLayout().

#include <QWidget>

class ModemEngine;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSettings;
class QSlider;
class QSplitter;
class WaterfallWidget;

class ModemPanel : public QWidget
{
    Q_OBJECT

public:
    ModemPanel(ModemEngine *engine, QSettings *settings, const QString &name, QWidget *parent = nullptr);
    virtual void saveState();

public slots:
    virtual void setRadioState(bool ready, bool canTransmit, const QString &mode);
    // From the shared spectrum engine.
    void addSpectrumLine(const QVector<float> &db, double hzPerBin);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void finishLayout(QWidget *top, QWidget *rx);
    virtual void syncSettings();
    virtual void syncTransmitting(bool on);
    virtual double markerBandwidth() const = 0;
    // Status line and mode warning, for the subclass's top area.
    QLabel *statusLabel() const
    {
        return m_status;
    }
    QLabel *modeWarningLabel() const
    {
        return m_modeWarning;
    }
    WaterfallWidget *waterfall() const
    {
        return m_waterfall;
    }
    QString name() const
    {
        return m_name;
    }
    void setCallFromReceive(const QString &word);

    ModemEngine *m_engine;
    QSettings *m_settings;
    bool m_ready = false;
    bool m_canTx = false;
    QString m_mode;

private slots:
    void syncMacros();
    void sendTyped();
    void editMacros();

private:
    QWidget *buildTx();
    void updateModeWarning();

    QString m_name;
    QSplitter *m_split = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_modeWarning = nullptr;
    WaterfallWidget *m_waterfall = nullptr;
    QLineEdit *m_call = nullptr;
    QLineEdit *m_myCall = nullptr;
    QList<QPushButton *> m_macroButtons;
    QPlainTextEdit *m_txText = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_txBtn = nullptr;
    QPushButton *m_rxBtn = nullptr;
    QPushButton *m_abort = nullptr;
    QCheckBox *m_hold = nullptr;
    QSlider *m_txLevel = nullptr;
    QLabel *m_txLevelValue = nullptr;
};
