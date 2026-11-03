#pragma once

// CW tab: decoder controls, decode pane and (optional) multi-signal list.

#include "CwDecoder.h"

#include <QWidget>

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
    CwPanel(CwDecoder *decoder, QSettings *settings, QWidget *parent = nullptr);
    void saveState();

private slots:
    void syncFromDecoder();
    void appendText(const QString &text);
    void showChannels(const QVector<CwChannelInfo> &channels);

private:
    void loadState();

    CwDecoder *m_dec;
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
};
