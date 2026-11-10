#pragma once

// WEFAX tab: Decode, LPM / IOC, automatic start on the APT tone, Start now /
// Stop, slant trim, alignment (click the picture's left edge), the growing
// picture and a small waterfall. Receive only.

#include <QWidget>

class ImageView;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class WaterfallWidget;
class WefaxEngine;

class WefaxPanel : public QWidget
{
    Q_OBJECT

public:
    explicit WefaxPanel(WefaxEngine *engine, QWidget *parent = nullptr);
    WaterfallWidget *waterfall() const
    {
        return m_wf;
    }

public slots:
    void addSpectrumLine(const QVector<float> &db, double hzPerBin);

private:
    void sync();
    void syncStatus();
    void showImage();

    WefaxEngine *m_fax;
    QPushButton *m_decode = nullptr;
    QComboBox *m_lpm = nullptr;
    QComboBox *m_ioc = nullptr;
    QCheckBox *m_auto = nullptr;
    QSpinBox *m_trim = nullptr;
    QPushButton *m_align = nullptr;
    QLabel *m_status = nullptr;
    ImageView *m_view = nullptr;
    QScrollArea *m_scroll = nullptr;
    WaterfallWidget *m_wf = nullptr;
};
