// PSK Reporter (https://pskreporter.info/pskdev): IPFIX datagrams over UDP to
// report.pskreporter.info port 4739 (test: pskreporter.info port 14739).
// Qt-free so it can be tested with g++.
//
// One datagram = 16-byte header, the receiver and sender templates (first three datagrams,
// then once an hour), one receiver record (every datagram) and the sender records.
// Receiver template 0x9992: receiverCallsign, receiverLocator, decoderSoftware
//   [, antennaInformation]. Sender template 0x9993: senderCallsign, frequency, mode,
//   informationSource, flowStartSeconds.
// Rules from the page: no more than one datagram every 5 minutes (unless one fills up),
// not aligned to the clock; each callsign at most once per 5 minutes, ideally once an hour
// unless something changed; sequence number goes up by the number of records sent.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pskreporter
{
constexpr std::uint16_t kReceiverTemplate = 0x9992;
constexpr std::uint16_t kSenderTemplate = 0x9993;
constexpr std::uint32_t kEnterprise = 30351; // 00 00 76 8F
constexpr const char *kHost = "report.pskreporter.info";
constexpr std::uint16_t kPort = 4739;
constexpr const char *kTestHost = "pskreporter.info";
constexpr std::uint16_t kTestPort = 14739;
constexpr int kSendIntervalSec = 300;
constexpr size_t kMaxDatagram = 1400; // stay under a usual MTU

struct Receiver
{
    std::string call, locator, software, antenna; // antenna empty: the 3-field template
};

struct Spot
{
    std::string call;
    std::uint32_t hz = 0;
    std::string mode; // ADIF MODE or SUBMODE: CW, RTTY, PSK31, OLIVIA ...
    std::uint8_t source = 1; // informationSource: 1 = automatically extracted
    std::uint32_t timeSec = 0; // flowStartSeconds (Unix)
};

namespace detail
{
inline void u8(std::vector<std::uint8_t> &b, unsigned v)
{
    b.push_back(std::uint8_t(v & 0xFF));
}
inline void u16(std::vector<std::uint8_t> &b, unsigned v)
{
    u8(b, v >> 8);
    u8(b, v);
}
inline void u32(std::vector<std::uint8_t> &b, std::uint32_t v)
{
    u16(b, v >> 16);
    u16(b, v & 0xFFFF);
}
inline void str(std::vector<std::uint8_t> &b, const std::string &s)
{
    const size_t n = s.size() > 254 ? 254 : s.size();
    u8(b, unsigned(n));
    b.insert(b.end(), s.begin(), s.begin() + long(n));
}
inline void pad4(std::vector<std::uint8_t> &b, size_t setStart)
{
    while ((b.size() - setStart) % 4)
        b.push_back(0);
}
inline void setLength(std::vector<std::uint8_t> &b, size_t setStart)
{
    const size_t len = b.size() - setStart;
    b[setStart + 2] = std::uint8_t(len >> 8);
    b[setStart + 3] = std::uint8_t(len & 0xFF);
}
// Enterprise field specifier: 0x8000 | id, length, enterprise number.
inline void ent(std::vector<std::uint8_t> &b, unsigned id, unsigned len)
{
    u16(b, 0x8000 | id);
    u16(b, len);
    u32(b, kEnterprise);
}
} // namespace detail

inline void appendTemplates(std::vector<std::uint8_t> &b, bool withAntenna)
{
    using namespace detail;
    // Receiver: options template set (ID 3), scope field count 1.
    size_t s = b.size();
    u16(b, 0x0003);
    u16(b, 0);
    u16(b, kReceiverTemplate);
    u16(b, withAntenna ? 4 : 3);
    u16(b, 1);
    ent(b, 2, 0xFFFF); // receiverCallsign
    ent(b, 4, 0xFFFF); // receiverLocator
    ent(b, 8, 0xFFFF); // decoderSoftware
    if (withAntenna)
        ent(b, 9, 0xFFFF); // antennaInformation
    pad4(b, s);
    setLength(b, s);
    // Sender: template set (ID 2).
    s = b.size();
    u16(b, 0x0002);
    u16(b, 0);
    u16(b, kSenderTemplate);
    u16(b, 5);
    ent(b, 1, 0xFFFF); // senderCallsign
    ent(b, 5, 4); // frequency
    ent(b, 10, 0xFFFF); // mode
    ent(b, 11, 1); // informationSource
    u16(b, 150); // flowStartSeconds (IANA, no enterprise number)
    u16(b, 4);
    pad4(b, s);
    setLength(b, s);
}

inline void appendReceiver(std::vector<std::uint8_t> &b, const Receiver &r)
{
    using namespace detail;
    const size_t s = b.size();
    u16(b, kReceiverTemplate);
    u16(b, 0);
    str(b, r.call);
    str(b, r.locator);
    str(b, r.software);
    if (!r.antenna.empty())
        str(b, r.antenna);
    pad4(b, s);
    setLength(b, s);
}

inline size_t spotSize(const Spot &sp)
{
    return 1 + std::min<size_t>(sp.call.size(), 254) + 4 + 1 + std::min<size_t>(sp.mode.size(), 254) + 1 + 4;
}

inline void appendSpots(std::vector<std::uint8_t> &b, const std::vector<Spot> &spots)
{
    using namespace detail;
    if (spots.empty())
        return;
    const size_t s = b.size();
    u16(b, kSenderTemplate);
    u16(b, 0);
    for (const Spot &sp : spots)
    {
        str(b, sp.call);
        u32(b, sp.hz);
        str(b, sp.mode);
        u8(b, sp.source);
        u32(b, sp.timeSec);
    }
    pad4(b, s);
    setLength(b, s);
}

// A whole datagram. sequence: records sent before this one (the page: it goes up by the
// number of records, receiver included).
inline std::vector<std::uint8_t> datagram(const Receiver &r,
                                          const std::vector<Spot> &spots,
                                          bool templates,
                                          std::uint32_t exportTime,
                                          std::uint32_t sequence,
                                          std::uint32_t randomId)
{
    using namespace detail;
    std::vector<std::uint8_t> b;
    u16(b, 0x000A);
    u16(b, 0); // length, below
    u32(b, exportTime);
    u32(b, sequence);
    u32(b, randomId);
    if (templates)
        appendTemplates(b, !r.antenna.empty());
    appendReceiver(b, r);
    appendSpots(b, spots);
    b[2] = std::uint8_t(b.size() >> 8);
    b[3] = std::uint8_t(b.size() & 0xFF);
    return b;
}

// Which spots to send, and when. Dedupe per callsign: not within 5 minutes; after that
// only if the band changed, or an hour has gone by.
class Queue
{
public:
    // A spot to report; false if it was reported too recently (or is already waiting).
    bool add(const Spot &sp, std::int64_t now, const std::string &band)
    {
        std::string call = sp.call;
        for (char &c : call)
            if (c >= 'a' && c <= 'z')
                c = char(c - 'a' + 'A');
        if (call.empty())
            return false;
        for (const Spot &w : m_waiting)
            if (w.call == call)
                return false;
        auto it = m_last.find(call);
        if (it != m_last.end())
        {
            const std::int64_t age = now - it->second.t;
            if (age < kSendIntervalSec)
                return false;
            if (age < 3600 && it->second.band == band)
                return false;
        }
        Spot s = sp;
        s.call = call;
        m_waiting.push_back(s);
        m_last[call] = {now, band};
        return true;
    }

    size_t waiting() const
    {
        return m_waiting.size();
    }

    // Spots for one datagram (as many as fit in kMaxDatagram with the given overhead).
    std::vector<Spot> take(size_t overhead)
    {
        std::vector<Spot> out;
        size_t size = overhead + 4 + 3; // set header + worst-case padding
        size_t n = 0;
        for (; n < m_waiting.size(); ++n)
        {
            const size_t add = spotSize(m_waiting[n]);
            if (size + add > kMaxDatagram && !out.empty())
                break;
            size += add;
            out.push_back(m_waiting[n]);
        }
        m_waiting.erase(m_waiting.begin(), m_waiting.begin() + long(n));
        return out;
    }

    void clear()
    {
        m_waiting.clear();
        m_last.clear();
    }

private:
    struct Last
    {
        std::int64_t t;
        std::string band;
    };
    std::vector<Spot> m_waiting;
    std::map<std::string, Last> m_last;
};
} // namespace pskreporter
