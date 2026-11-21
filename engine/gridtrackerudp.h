// WSJT-X UDP messages (the protocol GridTracker2, JTAlert, Log4OM ... listen to on port
// 2237), Qt-free so it can be tested with g++. Layout as in WSJT-X's NetworkMessage.hpp:
// QDataStream big-endian, schema 2; every message starts with magic 0xadbccbda, schema,
// type, id. Strings are "utf8": quint32 byte count then the bytes (0xffffffff = null).
// QTime is quint32 milliseconds since midnight; "float" fields are 8-byte doubles.
//
// Sent by wfview TCI Digi: Heartbeat (0), Status (1), Decode (2). Nothing is read back.
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
} // namespace wsjtx
