#include "CwTxEngine.h"

#include "modemtxsource.h"

#include <QSettings>
#include <algorithm>

CwTxEngine::CwTxEngine(QObject *parent)
    : ModemEngine(QStringLiteral("CWTX"), std::make_shared<CwTxSource>(), parent)
{
    m_carrierHz = 700.0;
    applyConfig();
}

QStringList CwTxEngine::defaultMacroNames() const
{
    return {QStringLiteral("CQ"),
            QStringLiteral("Answer"),
            QStringLiteral("5NN"),
            QStringLiteral("TU 73"),
            QStringLiteral("QRZ?"),
            QStringLiteral("AGN?"),
            QStringLiteral("BK"),
            QStringLiteral("Test")};
}

QStringList CwTxEngine::defaultMacroTexts() const
{
    return {QStringLiteral("CQ CQ CQ DE {MYCALL} {MYCALL} {MYCALL} K "),
            QStringLiteral("{CALL} DE {MYCALL} {MYCALL} K "),
            QStringLiteral("{CALL} TU UR 5NN 5NN BK "),
            QStringLiteral("{CALL} TU 73 DE {MYCALL} <SK> "),
            QStringLiteral("QRZ? DE {MYCALL} K "),
            QStringLiteral("AGN? AGN? "),
            QStringLiteral("BK "),
            QStringLiteral("VVV VVV TEST DE {MYCALL} ")};
}

void CwTxEngine::loadSettings(QSettings *s)
{
    if (s)
        m_wpm = std::clamp(s->value(QStringLiteral("CWTX/Wpm"), m_wpm).toInt(), 5, 60);
    ModemEngine::loadSettings(s);
}

void CwTxEngine::applyConfig()
{
    auto tx = std::static_pointer_cast<CwTxSource>(txSource());
    tx->setWpm(m_wpm);
    tx->setCarrier(m_carrierHz);
    tx->setLevel(float(txLevel()));
}

void CwTxEngine::setWpm(int wpm)
{
    wpm = std::clamp(wpm, 5, 60);
    if (wpm == m_wpm)
        return;
    m_wpm = wpm;
    save("Wpm", m_wpm);
    applyConfig();
    emit settingsChanged();
}
