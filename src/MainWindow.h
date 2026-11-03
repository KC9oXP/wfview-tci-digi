#pragma once

#include "TciClient.h"

#include <QMainWindow>
#include <QTimer>

class CwDecoder;
class CwPanel;
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
    void buildLogDock();
    void buildMenus();
    void applyTheme(bool dark);
    void updateRadioState();
    void saveAll();

    QSettings *m_settings;
    TciClient *m_client = nullptr;
    CwDecoder *m_cw = nullptr;
    RttyEngine *m_rtty = nullptr;
    CwPanel *m_cwPanel = nullptr;
    RttyPanel *m_rttyPanel = nullptr;

    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QPushButton *m_connect = nullptr;
    QCheckBox *m_autoReconnect = nullptr;
    QLabel *m_connState = nullptr;
    QLineEdit *m_freq = nullptr;
    QComboBox *m_mode = nullptr;
    QLabel *m_txLamp = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_diag = nullptr;

    QDockWidget *m_logDock = nullptr;
    QPlainTextEdit *m_log = nullptr;

    bool m_wantConnected = false;
    QTimer m_reconnect;
};
