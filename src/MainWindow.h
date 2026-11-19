#pragma once

#include "TciClient.h"

#include <QMainWindow>
#include <QTimer>

class AmtorEngine;
class AmtorPanel;
class AudioHub;
class CwDecoder;
class NavtexEngine;
class NavtexPanel;
class OliviaEngine;
class OliviaPanel;
class QsoLogDock;
class SessionLog;
class SstvEngine;
class SstvPanel;
class WaterfallWidget;
class WefaxEngine;
class WefaxPanel;
class WwvEngine;
class WwvPanel;
class CwPanel;
class FreqEdit;
class CwTxEngine;
class HellEngine;
class HellPanel;
class ModemEngine;
class PskEngine;
class PskPanel;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QTabWidget;
class RttyEngine;
class RttyPanel;
class SpectrumEngine;
class TxPowerWatch;
class TxTools;
class UpdateCheck;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QSettings *settings, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void toggleConnection();
    void onStateChanged(TciClient::State state, const QString &message);
    void onRadioInfo();
    void onFrequency(qint64 hz);
    void onModulation(const QString &mode);
    void onRadioTransmit(bool on);
    void log(const QString &line);

private:
    QWidget *buildConnectionBar();
    QWidget *buildTxHint();
    // Frequency presets for the tab showing (connection bar combo).
    struct Preset
    {
        QString name;
        qint64 hz = 0;
        QString sideband; // "USB", "LSB" or empty (keep the mode)
    };
    QString currentTabKey() const;
    QList<Preset> presets(const QString &tabKey) const;
    void savePresets(const QString &tabKey, const QList<Preset> &list);
    void fillPresetBox();
    void applyPreset(const Preset &p);
    void addCurrentPreset();
    void editPresets();
    void abortAllTx(const QString &why);
    QString alcKey() const;
    void loadAlcLimit();
    void editAlcLimit();
    void buildLogDock();
    void buildMenus();
    void buildFileMenu();
    void wireLogs();
    void updateQsoMode();
    QString stampText() const;
    void saveWindowShot();
    void saveWaterfallShot(WaterfallWidget *wf = nullptr);
    void playWav(double speed);
    void updateAudioStatus();
    void applyTheme(bool dark);
    void updateRadioState();
    void saveAll();
    void onTransmitRequested(QObject *who, bool on);
    bool anyTransmitting() const;

    QSettings *m_settings;
    TciClient *m_client = nullptr;
    CwDecoder *m_cw = nullptr;
    RttyEngine *m_rtty = nullptr;
    CwPanel *m_cwPanel = nullptr;
    RttyPanel *m_rttyPanel = nullptr;
    PskEngine *m_psk = nullptr;
    HellEngine *m_hell = nullptr;
    PskPanel *m_pskPanel = nullptr;
    HellPanel *m_hellPanel = nullptr;
    SpectrumEngine *m_spectrum = nullptr;
    CwTxEngine *m_cwTx = nullptr;
    OliviaEngine *m_olivia = nullptr;
    OliviaPanel *m_oliviaPanel = nullptr;
    NavtexEngine *m_navtex = nullptr;
    NavtexPanel *m_navtexPanel = nullptr;
    SstvEngine *m_sstv = nullptr;
    SstvPanel *m_sstvPanel = nullptr;
    WefaxEngine *m_wefax = nullptr;
    WefaxPanel *m_wefaxPanel = nullptr;
    WwvEngine *m_wwv = nullptr;
    WwvPanel *m_wwvPanel = nullptr;
    void updateWwvRadio();
    AmtorEngine *m_amtor = nullptr;
    AmtorPanel *m_amtorPanel = nullptr;
    TxTools *m_tools = nullptr;
    UpdateCheck *m_update = nullptr;
    QList<ModemEngine *> m_txEngines; // m_modems + tune tone + CW ID
    AudioHub *m_audio = nullptr;
    SessionLog *m_session = nullptr;
    QsoLogDock *m_qso = nullptr;
    QLabel *m_audioState = nullptr;
    QAction *m_recordAct = nullptr;
    QAction *m_stopPlayAct = nullptr;
    QList<ModemEngine *> m_modems; // PSK, Hell, CW transmit, Olivia, SSTV, AMTOR

    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QPushButton *m_connect = nullptr;
    QCheckBox *m_autoReconnect = nullptr;
    QLabel *m_connState = nullptr;
    FreqEdit *m_freq = nullptr;
    QComboBox *m_mode = nullptr;
    QLabel *m_txLamp = nullptr;
    QLabel *m_txMeter = nullptr;
    QWidget *m_txHint = nullptr;
    bool m_hintDismissed = false;
    QPushButton *m_logBtn = nullptr;
    QAction *m_sessionAct = nullptr;
    QComboBox *m_presetBox = nullptr;
    QTimer m_txLimit;
    int m_txLimitMin = 5;
    TxPowerWatch *m_powerWatch = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_diag = nullptr;

    QDockWidget *m_logDock = nullptr;
    QPlainTextEdit *m_log = nullptr;

    bool m_wantConnected = false;
    QTimer m_reconnect;
};
