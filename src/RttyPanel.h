#pragma once

// RTTY tab: settings + tuning scope on top, receive pane in the middle,
// macros and transmit box at the bottom. The three parts sit in a splitter
// so the user can trade receive and transmit space.

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSettings;
class QSlider;
class QSpinBox;
class QSplitter;
class RttyEngine;
class ScopeWidget;

class RttyPanel : public QWidget
{
    Q_OBJECT

public:
    RttyPanel(RttyEngine *engine, QSettings *settings, QWidget *parent = nullptr);
    void saveState();

public slots:
    void setRadioState(bool ready, bool canTransmit, const QString &mode);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void syncSettings();
    void syncMacros();
    void syncStats();
    void syncTransmitting(bool on);
    void appendText(const QString &text);
    void sendTyped();
    void editMacros();

private:
    QWidget *buildTop();
    QWidget *buildTx();
    void updateScopeIdle();
    void updateModeWarning();

    RttyEngine *m_rtty;
    QSettings *m_settings;
    bool m_ready = false;
    bool m_canTx = false;
    QString m_mode;

    QSplitter *m_split = nullptr;
    QPushButton *m_decode = nullptr;
    QSlider *m_squelch = nullptr;
    QLabel *m_squelchValue = nullptr;
    QPushButton *m_reverse = nullptr;
    QComboBox *m_baud = nullptr;
    QComboBox *m_shift = nullptr;
    QSpinBox *m_mark = nullptr;
    QLabel *m_space = nullptr;
    QComboBox *m_sideband = nullptr;
    QComboBox *m_stopBits = nullptr;
    QCheckBox *m_usos = nullptr;
    QComboBox *m_figures = nullptr;
    QLineEdit *m_myCall = nullptr;
    QProgressBar *m_markBar = nullptr;
    QProgressBar *m_spaceBar = nullptr;
    QProgressBar *m_qualityBar = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_modeWarning = nullptr;
    ScopeWidget *m_scope = nullptr;

    QPlainTextEdit *m_rx = nullptr;

    QLineEdit *m_call = nullptr;
    QList<QPushButton *> m_macroButtons;
    QPlainTextEdit *m_txText = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_txBtn = nullptr;
    QPushButton *m_rxBtn = nullptr;
    QPushButton *m_abort = nullptr;
    QCheckBox *m_hold = nullptr;
    QCheckBox *m_diddle = nullptr;
    QSlider *m_txLevel = nullptr;
    QLabel *m_txLevelValue = nullptr;
};
