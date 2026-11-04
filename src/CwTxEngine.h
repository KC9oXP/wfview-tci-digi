#pragma once

// CW transmit as audio (see engine/cwtxcore.h): the radio stays in a data or
// SSB mode and the app keys a shaped tone at the pitch set here. Receive is
// the CW decoder's job; this engine only transmits.

#include "ModemEngine.h"

class CwTxEngine : public ModemEngine
{
    Q_OBJECT

public:
    explicit CwTxEngine(QObject *parent = nullptr);

    void loadSettings(QSettings *settings) override;
    int wpm() const
    {
        return m_wpm;
    }

public slots:
    void pushAudio(const QVector<float> &, float) override { }
    void setWpm(int wpm);

protected:
    void applyConfig() override;
    void resetReceiver() override { }
    QString prepareText(const QString &expanded) const override
    {
        return expanded.toUpper();
    }
    QStringList defaultMacroNames() const override;
    QStringList defaultMacroTexts() const override;
    double prerollSeconds() const override
    {
        return 0.3; // key-up silence while the radio switches to transmit
    }
    double tailSeconds() const override
    {
        return 0.2;
    }

private:
    int m_wpm = 20;
};
