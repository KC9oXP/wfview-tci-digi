#pragma once

// Inverted sideband after an RSID burst (0.9.16-6).
//
// Olivia, Contestia and MT63 are always sent on upper sideband. On LSB the
// tones arrive in reverse order: the RSID burst is still found (the detector
// also looks for it mirrored) but the mode itself only prints garbage. When the
// burst was heard mirrored and the radio is on LSB, the app switches the radio
// to the matching USB data mode and moves the dial so the signal stays on the
// same audio frequency:
//   LSB: RF = dial - audio,   USB: RF = dial + audio
//   same audio f on USB: new dial = old dial - 2 f
// Example: 3.586000 DIGL, signal at 2003 Hz -> RF 3.583997 -> DIGU 3.581994.
//
// Qt-free.

#include "rsidcore.h"

#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace sidebandfix
{
// Modes where the sideband changes the decode and USB is the convention.
// (RTTY is sent on LSB by convention and has its own Reverse; BPSK, CW and
// Feld Hell decode on either sideband.)
inline bool sidebandMatters(const rsid::Mode &m)
{
    return m.target == rsid::Target::Olivia || m.target == rsid::Target::Mt63;
}

// Dial for USB that puts a signal heard on LSB at audioHz on the same audio
// frequency.
inline long long usbDialForLsbSignal(long long lsbDialHz, double audioHz)
{
    return lsbDialHz - 2 * std::llround(audioHz);
}

inline std::string upper(std::string s)
{
    std::string out;
    for (char c : s)
        if (c != ' ')
            out += c == '_' ? '-' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// The USB twin of an LSB mode name: DIGL -> DIGU, DATA-L -> DATA-U,
// LSB-D -> USB-D, PKTLSB -> PKTUSB, LSB -> USB. Keeps the original's case
// (wfview reports "digl"). Empty if it is not an LSB name.
inline std::string usbTwin(const std::string &lsbMode)
{
    std::string s = lsbMode;
    const std::string u = upper(s);
    auto fix = [&s](size_t pos, size_t len, const char *to)
    {
        const bool lower = std::islower(static_cast<unsigned char>(s[pos])) != 0;
        std::string t = to;
        if (lower)
            for (char &c : t)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        s.replace(pos, len, t);
        return s;
    };
    size_t p = u.find("LSB");
    if (p != std::string::npos)
    {
        // Positions in u and s match unless s has spaces; find in s case-insensitively.
        for (size_t i = 0; i + 3 <= s.size(); ++i)
            if (upper(s.substr(i, 3)) == "LSB")
                return fix(i, 3, "USB");
    }
    if (u == "DIGL" || u == "DATA-L" || u == "DATAL" || u == "PKT-L" || u == "PKTL")
        return fix(s.size() - 1, 1, "U");
    return std::string();
}

inline bool isUsbDataName(const std::string &mode)
{
    const std::string u = upper(mode);
    return u == "DIGU" || u == "DATA-U" || u == "DATAU" || u == "USB-D" || u == "USBD" || u == "PKTUSB"
        || u == "PKT-USB" || u == "PKTU" || u == "PKT-U" || u == "DATA-USB" || u == "D-USB" || u == "USB-D1"
        || u == "USB-D2" || u == "USB-D3";
}

// The mode to ask the radio for: the twin of the current LSB mode, spelled
// as the server lists it; else the first USB data mode in the list; else the
// twin as is (if the server lists nothing); else empty.
inline std::string usbModeFor(const std::string &currentLsb, const std::vector<std::string> &available)
{
    const std::string twin = usbTwin(currentLsb);
    if (available.empty())
        return twin;
    if (!twin.empty())
        for (const std::string &a : available)
            if (upper(a) == upper(twin))
                return a;
    for (const std::string &a : available)
        if (isUsbDataName(a))
            return a;
    return std::string();
}
} // namespace sidebandfix
