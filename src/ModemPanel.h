#pragma once

// Common layout for the PSK and Feld Hell tabs:
//   top (mode controls + waterfall) / receive pane / transmit area (ModemTxBox),
// in a vertical splitter. A subclass builds the top and the receive pane and
// then calls finishLayout().

#include <QWidget>

class ModemEngine;
class ModemTxBox;
class QLabel;
class QSettings;
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
    void finishLayout(QWidget *top, QWidget *rx);
    virtual void syncSettings();
    virtual void syncTransmitting(bool on);
    virtual double markerBandwidth() const = 0;
    QLabel *statusLabel() const
    {
        return m_status;
    }
    WaterfallWidget *waterfall() const
    {
        return m_waterfall;
    }
    ModemTxBox *txBox() const
    {
        return m_txBox;
    }
    QString name() const
    {
        return m_name;
    }
    void setCallFromReceive(const QString &word);

    ModemEngine *m_engine;
    QSettings *m_settings;

private:
    QString m_name;
    QSplitter *m_split = nullptr;
    QLabel *m_status = nullptr;
    WaterfallWidget *m_waterfall = nullptr;
    ModemTxBox *m_txBox = nullptr;
};
