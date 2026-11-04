#include "ModemEngine.h"

#include "modemtxsource.h"

#include <QSettings>
#include <algorithm>
#include <cmath>
#include <string>

namespace
{
constexpr int txPollMs = 40;
constexpr qint64 txStartTimeoutMs = 3000;
} // namespace

ModemEngine::ModemEngine(const QString &group, std::shared_ptr<ModemTxSource> tx, QObject *parent)
    : QObject(parent)
    , m_group(group)
    , m_tx(std::move(tx))
{
    m_txPoll.setInterval(txPollMs);
    connect(&m_txPoll, &QTimer::timeout, this, &ModemEngine::pollTx);
    m_status = tr("Off");
}

ModemEngine::~ModemEngine()
{
    m_tx->stop();
}

QStringList ModemEngine::defaultMacroNames() const
{
    return {QStringLiteral("CQ"),
            QStringLiteral("Answer"),
            QStringLiteral("Report"),
            QStringLiteral("TU 73"),
            QStringLiteral("QRZ?"),
            QStringLiteral("AGN?"),
            QStringLiteral("BTU"),
            QStringLiteral("Test")};
}

QStringList ModemEngine::defaultMacroTexts() const
{
    return {QStringLiteral("CQ CQ CQ de {MYCALL} {MYCALL} {MYCALL} pse k\n"),
            QStringLiteral("{CALL} {CALL} de {MYCALL} {MYCALL} {MYCALL} k\n"),
            QStringLiteral("{CALL} de {MYCALL} ur rst 599 599 btu\n"),
            QStringLiteral("{CALL} tu 73 de {MYCALL} sk\n"),
            QStringLiteral("QRZ? de {MYCALL} k\n"),
            QStringLiteral("AGN? AGN? k\n"),
            QStringLiteral("btu {CALL} de {MYCALL} k\n"),
            QStringLiteral("test test de {MYCALL}\n")};
}

void ModemEngine::initMacros()
{
    if (m_macroNames.size() == kMacroCount)
        return;
    m_macroNames = defaultMacroNames();
    m_macroTexts = defaultMacroTexts();
    while (m_macroNames.size() < kMacroCount)
        m_macroNames << QStringLiteral("M%1").arg(m_macroNames.size() + 1);
    while (m_macroTexts.size() < kMacroCount)
        m_macroTexts << QString();
}

void ModemEngine::loadSettings(QSettings *settings)
{
    initMacros();
    m_settings = settings;
    if (!m_settings)
    {
        applyConfig();
        return;
    }
    m_settings->beginGroup(m_group);
    m_carrierHz = m_settings->value(QStringLiteral("Carrier"), m_carrierHz).toDouble();
    m_txLevel = m_settings->value(QStringLiteral("TxLevel"), m_txLevel).toDouble();
    const QStringList names = m_settings->value(QStringLiteral("MacroNames")).toStringList();
    const QStringList texts = m_settings->value(QStringLiteral("MacroTexts")).toStringList();
    if (names.size() == kMacroCount && texts.size() == kMacroCount)
    {
        m_macroNames = names;
        m_macroTexts = texts;
    }
    m_settings->endGroup();
    // My call is shared by all modes (RTTY 0.1.0 kept its own; use it as the default).
    m_myCall = m_settings->value(QStringLiteral("Station/MyCall"), m_settings->value(QStringLiteral("RTTY/MyCall")))
                   .toString();

    m_carrierHz = std::clamp(m_carrierHz, kMinHz, kMaxHz);
    m_txLevel = std::clamp(m_txLevel, 0.0, 1.0);
    applyConfig();
    emit settingsChanged();
    emit macrosChanged();
}

void ModemEngine::save(const char *key, const QVariant &value)
{
    if (!m_settings)
        return;
    m_settings->beginGroup(m_group);
    m_settings->setValue(QString::fromLatin1(key), value);
    m_settings->endGroup();
}

void ModemEngine::saveMacros()
{
    save("MacroNames", m_macroNames);
    save("MacroTexts", m_macroTexts);
}

void ModemEngine::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged(s);
}

// ---- settings --------------------------------------------------------------

void ModemEngine::setDecoding(bool on)
{
    if (m_decoding == on)
        return;
    m_decoding = on;
    onDecodingChanged(on);
    emit settingsChanged();
}

void ModemEngine::onDecodingChanged(bool on)
{
    if (on)
    {
        resetReceiver();
        setStatus(m_ready ? tr("Listening...") : tr("Not connected - connect to wfview first."));
    }
    else
    {
        emit statsChanged();
        setStatus(tr("Off"));
    }
}

void ModemEngine::setCarrierHz(double hz)
{
    hz = std::clamp(hz, kMinHz, kMaxHz);
    if (std::fabs(hz - m_carrierHz) < 0.01)
        return;
    m_carrierHz = hz;
    save("Carrier", m_carrierHz);
    applyConfig();
    resetReceiver();
    emit settingsChanged();
}

void ModemEngine::trackCarrier(double hz)
{
    hz = std::clamp(hz, kMinHz, kMaxHz);
    if (std::fabs(hz - m_carrierHz) < 0.05)
        return;
    m_carrierHz = hz;
    if (!m_keyed)
        m_tx->setCarrier(hz); // transmit where we listen
    emit settingsChanged();
}

void ModemEngine::setTxLevel(double level)
{
    level = std::clamp(level, 0.0, 1.0);
    if (std::fabs(level - m_txLevel) < 1e-6)
        return;
    m_txLevel = level;
    save("TxLevel", m_txLevel);
    m_tx->setLevel(float(m_txLevel));
    emit settingsChanged();
}

void ModemEngine::setMyCall(const QString &c)
{
    const QString v = c.trimmed().toUpper();
    if (v == m_myCall)
        return;
    m_myCall = v;
    if (m_settings)
        m_settings->setValue(QStringLiteral("Station/MyCall"), m_myCall);
    emit settingsChanged();
}

void ModemEngine::setCall(const QString &c)
{
    const QString v = c.trimmed().toUpper();
    if (v == m_call)
        return;
    m_call = v;
    emit settingsChanged();
}

void ModemEngine::setMacro(int index, const QString &name, const QString &text)
{
    initMacros();
    if (index < 0 || index >= kMacroCount)
        return;
    m_macroNames[index] = name.trimmed().isEmpty() ? QStringLiteral("M%1").arg(index + 1) : name.trimmed();
    m_macroTexts[index] = text;
    saveMacros();
    emit macrosChanged();
}

void ModemEngine::setHoldTx(bool on)
{
    if (m_holdTx == on)
        return;
    m_holdTx = on;
    if (!on && m_keyed)
        m_autoRelease = true;
    emit settingsChanged();
}

// ---- receive ---------------------------------------------------------------

void ModemEngine::deliverText(const QString &text)
{
    if (m_keyed || !m_decoding || text.isEmpty())
        return;
    emit textAppended(text);
    m_atLineStart = text.endsWith(QLatin1Char('\n'));
}

// ---- transmit --------------------------------------------------------------

void ModemEngine::setConnected(bool ready, bool canTransmit)
{
    m_ready = ready;
    m_canTransmit = ready && canTransmit;
    if (!m_canTransmit && m_keyed)
        release(ready ? tr("Transmit not available") : tr("Connection lost"));
    if (m_decoding && m_status != tr("Off") && !m_keyed)
        setStatus(ready ? tr("Listening...") : tr("Not connected - connect to wfview first."));
}

void ModemEngine::setRadioTransmitting(bool on)
{
    m_radioTx = on;
    if (on && m_keyed)
        m_radioWasTx = true;
}

QString ModemEngine::expand(const QString &text) const
{
    QString t = text;
    t.replace(QStringLiteral("{MYCALL}"), m_myCall, Qt::CaseInsensitive);
    t.replace(QStringLiteral("{CALL}"), m_call, Qt::CaseInsensitive);
    return prepareText(t);
}

void ModemEngine::keyUp()
{
    if (m_keyed)
        return;
    if (!m_ready)
    {
        setStatus(tr("Not connected - connect to wfview first."));
        return;
    }
    if (!m_canTransmit)
    {
        setStatus(tr("The server reports this radio as receive only."));
        return;
    }
    applyConfig(); // carrier and level as shown now
    m_tx->start(prerollSeconds());
    m_keyed = true;
    m_tailQueued = false;
    m_radioWasTx = false;
    m_keyedTimer.start();
    emit transmittingChanged(true);
    emit transmitRequested(true);
    m_txPoll.start();
    emit textAppended((m_atLineStart ? QString() : QStringLiteral("\n")) + QStringLiteral("[TX] "));
    m_atLineStart = false;
    setStatus(tr("Transmitting on %1 Hz").arg(qRound(m_carrierHz)));
}

void ModemEngine::release(const QString &why)
{
    if (!m_keyed)
        return;
    const std::string sent = m_tx->takeSent();
    m_tx->stop();
    m_txPoll.stop();
    m_keyed = false;
    m_autoRelease = false;
    emit transmitRequested(false);
    emit transmittingChanged(false);
    QString tail = QString::fromLatin1(sent.data(), static_cast<int>(sent.size()));
    if (!tail.endsWith(QLatin1Char('\n')))
        tail += QLatin1Char('\n');
    emit textAppended(tail);
    m_atLineStart = true;
    resetReceiver();
    setStatus(why.isEmpty() ? (m_decoding ? tr("Listening...") : tr("Off")) : why);
}

void ModemEngine::send(const QString &text)
{
    const QString t = expand(text);
    if (t.isEmpty())
        return;
    if (!m_keyed)
    {
        keyUp();
        if (!m_keyed)
            return;
        m_autoRelease = !m_holdTx;
    }
    m_tx->queueText(t.toLatin1().toStdString());
    m_tailQueued = false;
}

void ModemEngine::sendMacro(int index)
{
    initMacros();
    if (index < 0 || index >= kMacroCount)
        return;
    const QString t = m_macroTexts.at(index);
    if (t.contains(QStringLiteral("{CALL}"), Qt::CaseInsensitive) && m_call.isEmpty())
    {
        setStatus(tr("Enter the other station's call first (the macro uses {CALL})."));
        return;
    }
    if (t.contains(QStringLiteral("{MYCALL}"), Qt::CaseInsensitive) && m_myCall.isEmpty())
    {
        setStatus(tr("Enter your own call in the My call field first (the macro uses {MYCALL})."));
        return;
    }
    send(t);
}

void ModemEngine::startTx()
{
    if (m_keyed)
    {
        m_autoRelease = false;
        return;
    }
    keyUp();
    m_autoRelease = false;
}

void ModemEngine::finishTx()
{
    if (m_keyed)
        m_autoRelease = true;
}

void ModemEngine::abortTx()
{
    release(tr("Transmit aborted"));
}

void ModemEngine::pollTx()
{
    if (!m_keyed)
    {
        m_txPoll.stop();
        return;
    }
    const std::string sent = m_tx->takeSent();
    if (!sent.empty())
        emit textAppended(QString::fromLatin1(sent.data(), static_cast<int>(sent.size())));

    const qint64 ms = m_keyedTimer.elapsed();
    if (ms > txStartTimeoutMs && m_tx->renderedFrames() == 0)
    {
        release(tr("No TX audio was requested by the server."));
        return;
    }
    if (ms > txStartTimeoutMs && !m_radioTx && !m_radioWasTx)
    {
        release(
            tr("The radio did not go to transmit (is Enable PTT Controls on in wfview's Settings > User Interface?)."));
        return;
    }
    if (m_radioWasTx && !m_radioTx)
    {
        release(tr("Transmit ended by the radio or wfview's PTT timeout."));
        return;
    }
    if (m_autoRelease && m_tx->idle())
    {
        if (!m_tailQueued)
        {
            m_tx->queueTail(tailSeconds());
            m_tailQueued = true;
        }
        else
        {
            release(QString());
        }
    }
}
