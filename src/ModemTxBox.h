#pragma once

// Transmit area shared by the PSK, Hell and CW tabs: mode warning, Call /
// My call, eight macro buttons (F1-F8 while the tab shows), the type-ahead
// box and Send / TX / RX / Abort / Hold TX / TX level.

#include <QWidget>

class ModemEngine;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;

class ModemTxBox : public QWidget
{
    Q_OBJECT

public:
    // name is the mode as shown to the user ("PSK", "Hell", "CW").
    ModemTxBox(ModemEngine *engine, const QString &name, QWidget *parent = nullptr);

    // Installs F1-F8 and Esc on shortcutParent (the whole tab).
    void installShortcuts(QWidget *shortcutParent);
    QLineEdit *callField() const
    {
        return m_call;
    }

public slots:
    void setRadioState(bool ready, bool canTransmit, const QString &mode);
    void syncSettings();
    void syncTransmitting(bool on);
    // Copies a word double-clicked in a receive pane into Call if it looks like a call sign.
    void offerCall(const QString &word);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void syncMacros();
    void sendTyped();
    void editMacros();

private:
    void updateModeWarning();

    ModemEngine *m_engine;
    QString m_name;
    bool m_ready = false;
    bool m_canTx = false;
    QString m_mode;

    QLabel *m_modeWarning = nullptr;
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
