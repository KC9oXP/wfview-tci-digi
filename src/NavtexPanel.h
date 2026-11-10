#pragma once

// NAVTEX tab: Decode, centre frequency, Reverse, the waterfall and the
// received messages. Receive only.

#include <QWidget>

class NavtexEngine;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class WaterfallWidget;

class NavtexPanel : public QWidget
{
    Q_OBJECT

public:
    explicit NavtexPanel(NavtexEngine *engine, QWidget *parent = nullptr);
    WaterfallWidget *waterfall() const
    {
        return m_wf;
    }

public slots:
    void addSpectrumLine(const QVector<float> &db, double hzPerBin);

private:
    void sync();
    void syncStats();

    NavtexEngine *m_nav;
    QPushButton *m_decode = nullptr;
    QDoubleSpinBox *m_freq = nullptr;
    QCheckBox *m_reverse = nullptr;
    QLabel *m_state = nullptr;
    WaterfallWidget *m_wf = nullptr;
    QPlainTextEdit *m_rx = nullptr;
};
