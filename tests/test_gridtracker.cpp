// g++ -std=c++17 -Wall -Iengine tests/test_gridtracker.cpp -o test_gridtracker
// GridTracker UDP messages (WSJT-X format, engine/gridtrackerudp.h) and the spotter's
// "how it was heard" labels used for the decode lines.
#include "callspot.h"
#include "gridtrackerudp.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
int g_fail = 0;
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (x)                                                                                                         \
            std::printf("  ok   %s\n", #x);                                                                            \
        else                                                                                                           \
        {                                                                                                              \
            std::printf("  FAIL %s (line %d)\n", #x, __LINE__);                                                        \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
    } while (0)

// Reads the same layout back, the way WSJT-X / GridTracker do.
struct Reader
{
    const std::vector<std::uint8_t> &b;
    size_t p = 0;
    bool bad = false;
    std::uint64_t un(int n)
    {
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i)
        {
            if (p >= b.size())
            {
                bad = true;
                return 0;
            }
            v = (v << 8) | b[p++];
        }
        return v;
    }
    std::uint32_t u32()
    {
        return std::uint32_t(un(4));
    }
    bool boolean()
    {
        return un(1) != 0;
    }
    std::string utf8()
    {
        const std::uint32_t n = u32();
        if (n == 0xffffffffU)
            return std::string();
        if (p + n > b.size())
        {
            bad = true;
            return std::string();
        }
        std::string s(b.begin() + long(p), b.begin() + long(p + n));
        p += n;
        return s;
    }
    double f64()
    {
        const std::uint64_t v = un(8);
        double d;
        std::memcpy(&d, &v, sizeof d);
        return d;
    }
    bool done() const
    {
        return !bad && p == b.size();
    }
};
} // namespace

int main()
{
    std::printf("heartbeat\n");
    {
        const auto b = wsjtx::heartbeat("wfview-tci-digi", "0.9.7");
        CHECK(b.size() >= 4 && b[0] == 0xad && b[1] == 0xbc && b[2] == 0xcb && b[3] == 0xda);
        Reader r{b};
        CHECK(r.u32() == wsjtx::kMagic);
        CHECK(r.u32() == 2);
        CHECK(r.u32() == wsjtx::Heartbeat);
        CHECK(r.utf8() == "wfview-tci-digi");
        CHECK(r.u32() == 3);
        CHECK(r.utf8() == "0.9.7");
        CHECK(r.utf8().empty());
        CHECK(r.done());
    }

    std::printf("status\n");
    {
        wsjtx::StatusInfo s;
        s.dialHz = 14080000;
        s.mode = "RTTY";
        s.decoding = true;
        s.rxDf = 1500;
        s.txDf = 1500;
        s.deCall = "KC9OXP";
        s.deGrid = "EN71ar";
        const auto b = wsjtx::status("wfview-tci-digi", s);
        Reader r{b};
        CHECK(r.u32() == wsjtx::kMagic && r.u32() == 2 && r.u32() == wsjtx::Status);
        CHECK(r.utf8() == "wfview-tci-digi");
        CHECK(r.un(8) == 14080000);
        CHECK(r.utf8() == "RTTY"); // mode
        CHECK(r.utf8().empty()); // DX call
        CHECK(r.utf8().empty()); // report
        CHECK(r.utf8() == "RTTY"); // Tx mode
        CHECK(!r.boolean()); // Tx enabled
        CHECK(!r.boolean()); // transmitting
        CHECK(r.boolean()); // decoding
        CHECK(r.u32() == 1500 && r.u32() == 1500);
        CHECK(r.utf8() == "KC9OXP");
        CHECK(r.utf8() == "EN71ar");
        CHECK(r.utf8().empty()); // DX grid
        CHECK(!r.boolean()); // watchdog
        CHECK(r.utf8().empty()); // sub-mode
        CHECK(!r.boolean()); // fast
        CHECK(r.un(1) == 0); // special op
        CHECK(r.u32() == 0xffffffffU && r.u32() == 0xffffffffU);
        CHECK(r.utf8() == "wfview TCI Digi");
        CHECK(r.utf8().empty()); // Tx message
        CHECK(r.done());
    }

    std::printf("decode\n");
    {
        wsjtx::DecodeInfo d;
        d.msSinceMidnightUtc = (4 * 3600 + 47 * 60 + 28) * 1000;
        d.snr = -7;
        d.dt = 0.0;
        d.df = 1530;
        d.mode = "CW";
        d.message = "CQ W1AW";
        const auto b = wsjtx::decode("wfview-tci-digi", d);
        Reader r{b};
        CHECK(r.u32() == wsjtx::kMagic && r.u32() == 2 && r.u32() == wsjtx::Decode);
        CHECK(r.utf8() == "wfview-tci-digi");
        CHECK(r.boolean()); // new
        CHECK(r.u32() == 17248000);
        CHECK(std::int32_t(r.u32()) == -7);
        CHECK(r.f64() == 0.0);
        CHECK(r.u32() == 1530);
        CHECK(r.utf8() == "CW");
        CHECK(r.utf8() == "CQ W1AW");
        CHECK(!r.boolean() && !r.boolean());
        CHECK(r.done());
        // a double goes out as 8 big-endian bytes: 1.5 = 3FF8000000000000
        wsjtx::Writer w;
        w.f64(1.5);
        CHECK(w.bytes.size() == 8 && w.bytes[0] == 0x3f && w.bytes[1] == 0xf8 && w.bytes[7] == 0);
    }

    std::printf("decode lines\n");
    CHECK(wsjtx::messageFor("W1AW", "CQ", "") == "CQ W1AW");
    CHECK(wsjtx::messageFor("W1AW", "DE", "K1ABC") == "K1ABC W1AW");
    CHECK(wsjtx::messageFor("W1AW", "DE", "") == "DE W1AW");
    CHECK(wsjtx::messageFor("W1AW", "73", "") == "DE W1AW");

    std::printf("spotter labels and the 1-minute repeat\n");
    {
        callspot::Spotter sp;
        sp.needed = 1;
        sp.windowSec = 60;
        sp.myCall = "KC9OXP";
        auto out = sp.feed("CW", "CQ CQ DE W1AW W1AW K ", 7030000, 0);
        CHECK(out.size() == 1 && out[0].call == "W1AW" && out[0].via == "CQ" && out[0].to.empty());
        out = sp.feed("CW", "K1ABC DE W6EL W6EL K ", 7040000, 0);
        CHECK(out.size() == 1 && out[0].via == "DE" && out[0].to == "K1ABC");
        out = sp.feed("RTTY", "CQ TEST N0AX N0AX ", 14080000, 0);
        CHECK(out.size() == 1 && out[0].via == "CQ");
        out = sp.feed("PSK", "K1ABC DE W4XYZ PSE K ", 14070000, 0);
        CHECK(out.size() == 1 && out[0].call == "W4XYZ" && out[0].to == "K1ABC");
        out = sp.feed("CW", "TU 73 K5ZZ SK ", 7031000, 0);
        CHECK(out.size() == 1 && out[0].call == "K5ZZ" && out[0].via == "73");
        // calling me: "KC9OXP DE W4XYZ" keeps KC9OXP as the call it was sent to
        callspot::Spotter me;
        me.myCall = "KC9OXP";
        out = me.feed("CW", "KC9OXP DE W9ABC K ", 7030000, 0);
        CHECK(out.size() == 1 && out[0].to == "KC9OXP");
        // same call again within a minute: not again; after a minute: again
        CHECK(sp.feed("CW", "CQ DE W1AW K ", 7030000, 30).empty());
        CHECK(sp.feed("CW", "CQ DE W1AW K ", 7030000, 61).size() == 1);
    }

    std::printf("My grid as WSJT-X sends it (GridTracker put \"en71ar\" at 0 N 0 E)\n");
    CHECK(wsjtx::normalGrid("en71ar") == "EN71ar");
    CHECK(wsjtx::normalGrid("EN71AR") == "EN71ar");
    CHECK(wsjtx::normalGrid(" en71 ") == "EN71");
    CHECK(wsjtx::normalGrid("EN71a").empty());
    CHECK(wsjtx::normalGrid("ZZ71ar").empty()); // field letters only go to R
    CHECK(wsjtx::normalGrid("EN71zz").empty()); // subsquare letters only go to X
    CHECK(wsjtx::normalGrid("").empty());
    {
        wsjtx::StatusInfo st;
        st.deGrid = wsjtx::normalGrid("en71ar");
        const auto b = wsjtx::status("wfview-tci-digi", st);
        const std::string raw(b.begin(), b.end());
        CHECK(raw.find("EN71ar") != std::string::npos);
        CHECK(raw.find("en71ar") == std::string::npos);
    }

    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
