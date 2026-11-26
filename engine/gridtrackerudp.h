// WSJT-X UDP messages (the protocol GridTracker2, JTAlert, Log4OM ... listen to on port
// 2237), Qt-free so it can be tested with g++. Layout as in WSJT-X's NetworkMessage.hpp:
// QDataStream big-endian, schema 2; every message starts with magic 0xadbccbda, schema,
// type, id. Strings are "utf8": quint32 byte count then the bytes (0xffffffff = null).
// QTime is quint32 milliseconds since midnight; "float" fields are 8-byte doubles.
//
// Sent by wfview TCI Digi: Heartbeat (0), Status (1), Decode (2), WSPRDecode (10).
// Nothing is read back.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wsjtx
{
constexpr std::uint32_t kMagic = 0xadbccbdaU;
constexpr std::uint32_t kSchema = 2;

enum Type : std::uint32_t
{
    Heartbeat = 0,
    Status = 1,
    Decode = 2,
    WsprDecode = 10,
};

class Writer
{
public:
    std::vector<std::uint8_t> bytes;

    void u8(std::uint8_t v)
    {
        bytes.push_back(v);
    }
    void boolean(bool v)
    {
        u8(v ? 1 : 0);
    }
    void u32(std::uint32_t v)
    {
        for (int s = 24; s >= 0; s -= 8)
            u8(std::uint8_t(v >> s));
    }
    void i32(std::int32_t v)
    {
        u32(std::uint32_t(v));
    }
    void u64(std::uint64_t v)
    {
        for (int s = 56; s >= 0; s -= 8)
            u8(std::uint8_t(v >> s));
    }
    void f64(double d)
    {
        std::uint64_t v;
        std::memcpy(&v, &d, sizeof v);
        u64(v);
    }
    void utf8(const std::string &s)
    {
        u32(std::uint32_t(s.size()));
        bytes.insert(bytes.end(), s.begin(), s.end());
    }
    void header(Type t, const std::string &id)
    {
        u32(kMagic);
        u32(kSchema);
        u32(t);
        utf8(id);
    }
};

inline std::vector<std::uint8_t> heartbeat(const std::string &id, const std::string &version)
{
    Writer w;
    w.header(Heartbeat, id);
    w.u32(3); // maximum schema understood
    w.utf8(version);
    w.utf8(""); // revision
    return w.bytes;
}

struct StatusInfo
{
    std::uint64_t dialHz = 0;
    std::string mode; // CW, RTTY, PSK31, OLIVIA ...
    std::string dxCall;
    bool transmitting = false;
    bool decoding = false;
    std::uint32_t rxDf = 0; // audio Hz of the signal being decoded
    std::uint32_t txDf = 0;
    std::string deCall;
    std::string deGrid;
};

inline std::vector<std::uint8_t> status(const std::string &id, const StatusInfo &s)
{
    Writer w;
    w.header(Status, id);
    w.u64(s.dialHz);
    w.utf8(s.mode);
    w.utf8(s.dxCall);
    w.utf8(""); // report
    w.utf8(s.mode); // Tx mode
    w.boolean(false); // Tx enabled (WSJT-X's Enable Tx button; there is none here)
    w.boolean(s.transmitting);
    w.boolean(s.decoding);
    w.u32(s.rxDf);
    w.u32(s.txDf);
    w.utf8(s.deCall);
    w.utf8(s.deGrid);
    w.utf8(""); // DX grid
    w.boolean(false); // Tx watchdog
    w.utf8(""); // sub-mode
    w.boolean(false); // fast mode
    w.u8(0); // special operation mode: none
    w.u32(0xffffffffU); // frequency tolerance: not used
    w.u32(0xffffffffU); // T/R period: not used
    w.utf8("wfview TCI Digi"); // configuration name
    w.utf8(""); // Tx message
    return w.bytes;
}

struct DecodeInfo
{
    std::uint32_t msSinceMidnightUtc = 0;
    std::int32_t snr = 0;
    double dt = 0.0;
    std::uint32_t df = 0; // audio Hz
    std::string mode;
    std::string message; // "CQ W1AW", "K1ABC W1AW"
};

inline std::vector<std::uint8_t> decode(const std::string &id, const DecodeInfo &d)
{
    Writer w;
    w.header(Decode, id);
    w.boolean(true); // new
    w.u32(d.msSinceMidnightUtc);
    w.i32(d.snr);
    w.f64(d.dt);
    w.u32(d.df);
    w.utf8(d.mode);
    w.utf8(d.message);
    w.boolean(false); // low confidence
    w.boolean(false); // off air
    return w.bytes;
}

// A WSPR spot (WSJT-X's WSPRDecode): the station, its grid and power as decoded.
struct WsprInfo
{
    std::uint32_t msSinceMidnightUtc = 0; // start of the slot
    std::int32_t snr = 0;
    double dt = 0.0;
    std::uint64_t rfHz = 0; // of the signal
    std::int32_t drift = 0; // Hz per minute
    std::string call;
    std::string grid;
    std::int32_t dbm = 0;
};

inline std::vector<std::uint8_t> wsprDecode(const std::string &id, const WsprInfo &d)
{
    Writer w;
    w.header(WsprDecode, id);
    w.boolean(true); // new
    w.u32(d.msSinceMidnightUtc);
    w.i32(d.snr);
    w.f64(d.dt);
    w.u64(d.rfHz);
    w.i32(d.drift);
    w.utf8(d.call);
    w.utf8(d.grid);
    w.i32(d.dbm);
    w.boolean(false); // off air
    return w.bytes;
}

// The decode line GridTracker reads for a heard call: "CQ W1AW" for a CQ, "K1ABC W1AW"
// when the call it was sent to is known (WSJT-X order: to, from), "DE W1AW" otherwise.
inline std::string messageFor(const std::string &call, const std::string &via, const std::string &to)
{
    if (via == "CQ")
        return "CQ " + call;
    if (!to.empty())
        return to + " " + call;
    return "DE " + call;
}
// Grid as WSJT-X sends it: "EN71ar" (field and square upper case, subsquare lower case),
// 4 or 6 characters. Anything that is not a valid Maidenhead locator gives "" (GridTracker
// puts an unreadable grid at 0 N 0 E, in the Gulf of Guinea).
inline std::string normalGrid(const std::string &in)
{
    std::string g;
    for (char c : in)
        if (c != ' ' && c != '\t')
            g.push_back(c);
    if (g.size() != 4 && g.size() != 6)
        return std::string();
    auto up = [](char c) { return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c; };
    auto low = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
    g[0] = up(g[0]);
    g[1] = up(g[1]);
    if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R' || g[2] < '0' || g[2] > '9' || g[3] < '0'
        || g[3] > '9')
        return std::string();
    if (g.size() == 6)
    {
        g[4] = low(g[4]);
        g[5] = low(g[5]);
        if (g[4] < 'a' || g[4] > 'x' || g[5] < 'a' || g[5] > 'x')
            return std::string();
    }
    return g;
}

} // namespace wsjtx
