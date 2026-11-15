#pragma once

// What a radio mode name (as the TCI server reports it) means for sending
// audio. wfview reports Icom data modes as DIGU / DIGL; its FT-991A build
// reports DATA-U / DATA-L; other servers use USB-D, LSB-D, PKTUSB, DIGL ...

#include <QRegularExpression>
#include <QString>

namespace RadioModes
{
enum class Kind
{
    Unknown, // nothing reported yet
    Data, // a data mode on SSB: the right one for sending audio
    Voice, // plain USB / LSB
    Cw,
    Other // AM, FM, RTTY (FSK), ...
};

inline QString normalised(const QString &mode)
{
    QString m = mode.trimmed().toUpper();
    m.remove(QLatin1Char(' '));
    m.replace(QLatin1Char('_'), QLatin1Char('-'));
    return m;
}

inline Kind kind(const QString &mode)
{
    const QString m = normalised(mode);
    if (m.isEmpty())
        return Kind::Unknown;
    if (m.startsWith(QLatin1String("CW")))
        return Kind::Cw;
    if (m == QLatin1String("USB") || m == QLatin1String("LSB"))
        return Kind::Voice;
    static const QRegularExpression dataRe(
        QStringLiteral("^(DIG[UL]?|DATA-?[UL]|DATA-?(USB|LSB)|(USB|LSB)-?D[0-9]?|PKT-?(USB|LSB|U|L)|D-?(USB|LSB))$"));
    if (dataRe.match(m).hasMatch())
        return Kind::Data;
    return Kind::Other;
}

inline bool isData(const QString &mode)
{
    return kind(mode) == Kind::Data;
}

// Lower sideband (audio tones inverted on the air).
inline bool isLsb(const QString &mode)
{
    const QString m = normalised(mode);
    if (m.contains(QLatin1String("LSB")))
        return true;
    return m == QLatin1String("DIGL") || m == QLatin1String("DATA-L") || m == QLatin1String("DATAL")
        || m == QLatin1String("PKT-L") || m == QLatin1String("PKTL");
}

// Sending audio works in a data mode or plain USB / LSB.
inline bool carriesAudio(const QString &mode)
{
    const Kind k = kind(mode);
    return k == Kind::Data || k == Kind::Voice;
}
} // namespace RadioModes
