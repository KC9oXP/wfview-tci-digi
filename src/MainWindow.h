#pragma once

#include "TciClient.h"
#include "inputlevel.h"
#include "memories.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include <cmath>

class AmtorEngine;
class AmtorPanel;
class AudioHub;
class CwDecoder;
class InputLevelMeter;
class NavtexEngine;
class NavtexPanel;
class OliviaEngine;
class OliviaPanel;
class Mt63Engine;
class Mt63Panel;
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
class MemoriesDialog;
class PskReporter;
class LogUploader;
class GridTrackerLink;
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
class RsidEngine;
class TxPowerWatch;
class TxTools;
class CallLookup;
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
    void onRsid(int code, double centreHz, bool mirrored, int errors);
    void checkRsidReturn();
    // A dot on a tab that is not showing when its decoder prints text.
    void markActivity(QWidget *panel);
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
    void syncPresetBox(); // show the preset the dial is on, else "Presets"
    // Tab order: drag tabs to rearrange (View > Reset tab order). Kept in Window/TabOrder by key.
    void arrangeTabs(const QStringList &order);
    void tabsMoved();
    QStringList tabOrder() const;
    QList<Preset> presets(const QString &tabKey) const;
    void savePresets(const QString &tabKey, const QList<Preset> &list);
    void fillPresetBox();
    void applyPreset(const Preset &p, const QString &what = QString());
    // Software memories (Memories button, Ctrl+M)
    void showMemories();
    void applyMemory(const memories::Memory &m);
    memories::Memory currentMemory() const;
    QString applySubMode(const QString &tab, const QString &sub);
    // PSK Reporter (status bar button, Options > PSK Reporter...)
    void wirePskReporter();
    void updateGridTracker(); // dial, mode, TX state, audio offset (sent only on change)
    void updatePskrButton();
    void editPskReporter();
    void togglePskReporter();
    qint64 rfHz(double audioHz) const; // RF frequency of a tone at audioHz in the passband
    qint64 currentSignalHz() const;
    // Audio frequency of the signal on the tab in use and how wide it is (for its SNR); 0 if none.
    double currentAudioHz(double *halfWidthHz = nullptr) const;
    void trackSnr(const QVector<float> &db, double hzPerBin);
    QString signalNote() const;
    void updateSnrLabel();
    void updateInputLevel();
    void editCallLookup();
    QString currentSubMode() const;
    void feedPskr(const QString &tab, const QString &subMode, const QString &text, double audioHz);
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
    RsidEngine *m_rsid = nullptr;
    CwTxEngine *m_cwTx = nullptr;
    OliviaEngine *m_olivia = nullptr;
    OliviaPanel *m_oliviaPanel = nullptr;
    Mt63Engine *m_mt63 = nullptr;
    Mt63Panel *m_mt63Panel = nullptr;
    // RSID jump: the tab to go back to when the signal it found has ended.
    QWidget *m_rsidFrom = nullptr;
    QWidget *m_rsidTo = nullptr;
    qint64 m_rsidJumpMs = 0; // when it jumped
    qint64 m_rsidTextMs = 0; // last text on the tab it jumped to (0 = none yet)
    QTimer *m_rsidReturn = nullptr;
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
    CallLookup *m_lookup = nullptr;
    double m_snr = std::nan(""); // smoothed SNR of the signal being decoded
    double m_snrHz = 0.0; // audio frequency it was measured at
    QElapsedTimer m_snrAge;
    QLabel *m_snrLabel = nullptr; // live SNR of the signal on the tab in use
    QTimer m_snrShow;
    InputLevelMeter *m_inLevel = nullptr; // RX input dBFS, top bar, same for every tab
    inlevel::Meter m_inMeter;               // RX audio since the last reading
    inlevel::PeakHold m_inHold;
    QElapsedTimer m_inClock;
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
    QPushButton *m_memBtn = nullptr;
    MemoriesDialog *m_memories = nullptr;
    PskReporter *m_pskr = nullptr;
    LogUploader *m_uploader = nullptr;
    GridTrackerLink *m_gridTracker = nullptr; // live dial / mode / heard calls to GridTracker2
    QPushButton *m_qrzBtn = nullptr;
    QPushButton *m_lotwBtn = nullptr;
    void updateUploadButtons();
    QPushButton *m_pskrBtn = nullptr;
    QTimer m_pskrRefresh;
    QTimer m_txLimit;
    int m_txLimitMin = 5;
    TxPowerWatch *m_powerWatch = nullptr;
    QTabWidget *m_tabs = nullptr;
    QWidget *m_tabDivider = nullptr; // "Read only" tab: transmit tabs left of it, receive-only right
    bool m_arrangingTabs = false;
    bool m_tabsDragged = false; // a tab moved in this drag; tidy and save on release
    QLabel *m_diag = nullptr;

    QDockWidget *m_logDock = nullptr;
    QPlainTextEdit *m_log = nullptr;

    bool m_wantConnected = false;
    QTimer m_reconnect;
};
