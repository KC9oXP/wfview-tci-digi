// Qt-free tests of PSK Reporter datagrams and of callsign spotting in decoded text.
// Build: g++ -std=c++17 -O2 -Wall -Iengine tests/test_pskreporter.cpp -o test_pskreporter

#include "callspot.h"
#include "pskrcore.h"

#include <cstdio>

static int g_fail = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::printf("  FAIL line %d: %s\n", __LINE__, #cond);                                                      \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
        else                                                                                                           \
            std::printf("  ok   %s\n", #cond);                                                                         \
    } while (0)

int main()
{
    std::printf("datagram: the sample on pskreporter.info/pskdev, byte for byte\n");
    {
        // Sequence 1, export time 0x47953272, random ID 0; receiver N1DQ FN42hn "Homebrew v5.6";
        // two PSK spots (N1DQ 14070567 Hz, KB1MBX 14070987 Hz).
        static const std::uint8_t expect[] = {0x00,
                                              0x0A,
                                              0x00,
                                              0xAC,
                                              0x47,
                                              0x95,
                                              0x32,
                                              0x72,
                                              0x00,
                                              0x00,
                                              0x00,
                                              0x01,
                                              0x00,
                                              0x00,
                                              0x00,
                                              0x00,
                                              // receiver options template
                                              0x00,
                                              0x03,
                                              0x00,
                                              0x24,
                                              0x99,
                                              0x92,
                                              0x00,
                                              0x03,
                                              0x00,
                                              0x01,
                                              0x80,
                                              0x02,
                                              0xFF,
                                              0xFF,
                                              0x00,
                                              0x00,
                                              0x76,
                                              0x8F,
                                              0x80,
                                              0x04,
                                              0xFF,
                                              0xFF,
                                              0x00,
                                              0x00,
                                              0x76,
                                              0x8F,
                                              0x80,
                                              0x08,
                                              0xFF,
                                              0xFF,
                                              0x00,
                                              0x00,
                                              0x76,
                                              0x8F,
                                              0x00,
                                              0x00,
                                              // sender template
                                              0x00,
                                              0x02,
                                              0x00,
                                              0x2C,
                                              0x99,
                                              0x93,
                                              0x00,
                                              0x05,
                                              0x80,
                                              0x01,
                                              0xFF,
                                              0xFF,
                                              0x00,
                                              0x00,
                                              0x76,
                                              0x8F,
                                              0x80,
                                              0x05,
                                              0x00,
                                              0x04,
                                              0x00,
                                              0x00,
                                              0x76,
                                              0x8F,
                                              0x80,
                                              0x0A,
                                              0xFF,
                                              0xFF,
                                              0x00,
                                              0x00,
                                              0x76,
                                              0x8F,
                                              0x80,
                                              0x0B,
                                              0x00,
                                              0x01,
                                              0x00,
                                              0x00,
                                              0x76,
                                              0x8F,
                                              0x00,
                                              0x96,
                                              0x00,
                                              0x04,
                                              // receiver record
                                              0x99,
                                              0x92,
                                              0x00,
                                              0x20,
                                              0x04,
                                              0x4E,
                                              0x31,
                                              0x44,
                                              0x51,
                                              0x06,
                                              0x46,
                                              0x4E,
                                              0x34,
                                              0x32,
                                              0x68,
                                              0x6E,
                                              0x0D,
                                              0x48,
                                              0x6F,
                                              0x6D,
                                              0x65,
                                              0x62,
                                              0x72,
                                              0x65,
                                              0x77,
                                              0x20,
                                              0x76,
                                              0x35,
                                              0x2E,
                                              0x36,
                                              0x00,
                                              0x00,
                                              // sender records
                                              0x99,
                                              0x93,
                                              0x00,
                                              0x2C,
                                              0x04,
                                              0x4E,
                                              0x31,
                                              0x44,
                                              0x51,
                                              0x00,
                                              0xD6,
                                              0xB3,
                                              0x27,
                                              0x03,
                                              0x50,
                                              0x53,
                                              0x4B,
                                              0x01,
                                              0x47,
                                              0x95,
                                              0x32,
                                              0x54,
                                              0x06,
                                              0x4B,
                                              0x42,
                                              0x31,
                                              0x4D,
                                              0x42,
                                              0x58,
                                              0x00,
                                              0xD6,
                                              0xB4,
                                              0xCB,
                                              0x03,
                                              0x50,
                                              0x53,
                                              0x4B,
                                              0x01,
                                              0x47,
                                              0x95,
                                              0x32,
                                              0x68,
                                              0x00,
                                              0x00};
        pskreporter::Receiver r {"N1DQ", "FN42hn", "Homebrew v5.6", ""};
        std::vector<pskreporter::Spot> spots
            = {{"N1DQ", 14070567, "PSK", 1, 0x47953254}, {"KB1MBX", 14070987, "PSK", 1, 0x47953268}};
        const auto d = pskreporter::datagram(r, spots, true, 0x47953272, 1, 0);
        CHECK(d.size() == sizeof(expect));
        bool same = d.size() == sizeof(expect);
        for (size_t i = 0; same && i < d.size(); ++i)
            if (d[i] != expect[i])
            {
                std::printf("  byte %zu: got %02X want %02X\n", i, d[i], expect[i]);
                same = false;
            }
        CHECK(same);
        // Later datagram without templates: header + receiver + senders.
        const auto d2 = pskreporter::datagram(r, spots, false, 0x47953272, 4, 0);
        CHECK(d2.size() == 16 + 32 + 44);
        CHECK(d2[11] == 4 && d2[2] == 0 && d2[3] == 92);
    }

    std::printf("datagram with antenna: 4-field receiver template\n");
    {
        pskreporter::Receiver r {"KC9OXP", "EN71ar", "wfview TCI Digi 0.7.0", "Random wire"};
        const auto d = pskreporter::datagram(r, {}, true, 1, 1, 0x12345678);
        CHECK(d[16] == 0x00 && d[17] == 0x03 && d[18] == 0x00 && d[19] == 0x2C); // set length 0x2C
        CHECK(d[23] == 4); // field count
        CHECK(d.size() % 4 == 0);
        CHECK(d[12] == 0x12 && d[15] == 0x78);
    }

    std::printf("queue: once per 5 minutes, then only on a band change or after an hour\n");
    {
        pskreporter::Queue q;
        pskreporter::Spot s {"w1aw", 14070000, "PSK31", 1, 0};
        CHECK(q.add(s, 0, "20m"));
        CHECK(!q.add(s, 10, "20m")); // waiting already
        auto out = q.take(100);
        CHECK(out.size() == 1 && out[0].call == "W1AW");
        CHECK(!q.add(s, 200, "40m")); // < 5 min even on another band
        CHECK(!q.add(s, 400, "20m")); // same band, < 1 h
        CHECK(q.add(s, 400, "40m")); // band changed, > 5 min
        q.take(100);
        CHECK(!q.add(s, 3000, "40m"));
        CHECK(q.add(s, 400 + 3600, "40m")); // an hour later
        // many spots split over datagrams of at most kMaxDatagram
        pskreporter::Queue big;
        for (int i = 0; i < 200; ++i)
            big.add(
                {"K" + std::to_string(i % 10) + "AB" + char('A' + i % 26) + char('A' + i / 26), 7070000, "RTTY", 1, 0},
                0,
                "40m");
        CHECK(big.waiting() == 200);
        pskreporter::Receiver r {"KC9OXP", "EN71ar", "wfview TCI Digi", ""};
        const size_t overhead = pskreporter::datagram(r, {}, true, 0, 0, 0).size();
        const auto part = big.take(overhead);
        const auto d = pskreporter::datagram(r, part, true, 0, 0, 0);
        std::printf("  first datagram: %zu spots, %zu bytes\n", part.size(), d.size());
        CHECK(d.size() <= pskreporter::kMaxDatagram && part.size() > 40 && part.size() < 200);
        CHECK(big.waiting() == 200 - part.size());
    }

    std::printf("isCallsign\n");
    using callspot::isCallsign;
    CHECK(isCallsign("W1AW"));
    CHECK(isCallsign("KC9OXP"));
    CHECK(isCallsign("kc9oxp"));
    CHECK(isCallsign("2E0ABC"));
    CHECK(isCallsign("9A1A"));
    CHECK(isCallsign("3DA0XY"));
    CHECK(isCallsign("VE3/KC9OXP"));
    CHECK(isCallsign("KC9OXP/P"));
    CHECK(isCallsign("KC9OXP/QRP"));
    CHECK(isCallsign("W6EL"));
    CHECK(isCallsign("K3WW"));
    CHECK(!isCallsign("5NN"));
    CHECK(!isCallsign("599"));
    CHECK(!isCallsign("73"));
    CHECK(!isCallsign("TU"));
    CHECK(!isCallsign("QSO"));
    CHECK(!isCallsign("UR5")); // no suffix
    CHECK(!isCallsign("RST"));
    CHECK(!isCallsign("KC9OXP/"));
    CHECK(!isCallsign("A/B/C/D"));
    CHECK(!isCallsign("14070"));

    std::printf("spotter: three sightings after DE / CQ\n");
    {
        callspot::Spotter sp;
        sp.myCall = "KC9OXP";
        // all in one go
        auto out = sp.feed("PSK", "CQ CQ CQ DE W1AW W1AW W1AW K\n", 14071000, 100);
        CHECK(out.size() == 1 && out[0].call == "W1AW" && out[0].source == "PSK" && out[0].hz == 14071000);
        // not again straight away
        out = sp.feed("PSK", "CQ DE W1AW W1AW W1AW K ", 14071000, 160);
        CHECK(out.empty());
        // pieces arriving a few letters at a time
        int n = 0;
        const std::string text = "CQ CQ DE K3WW K3WW PSE K\nCQ DE K3WW K ";
        for (size_t i = 0; i < text.size(); i += 3)
            n += int(sp.feed("RTTY", text.substr(i, 3), 7080000, 200).size());
        CHECK(n == 1);
        // only twice: not yet
        out = sp.feed("CW", "CQ DE W6EL W6EL K ", 7030000, 300);
        CHECK(out.empty() && sp.sightings("W6EL", 300) == 2);
        // third, but far away in frequency: does not count with the others
        out = sp.feed("CW", "DE W6EL ", 14030000, 310);
        CHECK(out.empty());
        // the third close by, but after the 10 minute window: starts again
        callspot::Spotter late;
        late.feed("CW", "CQ DE W6EL W6EL ", 7030000, 0);
        CHECK(late.feed("CW", "DE W6EL ", 7030000, 700).empty());
        // "CQ DX" / "CQ TEST" / "CQ POTA" before the call
        callspot::Spotter dx;
        CHECK(dx.feed("RTTY", "CQ TEST W1AW W1AW CQ TEST W1AW ", 14080000, 0).size() == 1);
        // my own call is never reported
        CHECK(sp.feed("PSK", "CQ DE KC9OXP KC9OXP KC9OXP ", 14070000, 400).empty());
        // a call that is not after DE or CQ does not count
        callspot::Spotter none;
        CHECK(none.feed("PSK", "W1AW W1AW W1AW W1AW TNX FER QSO ", 14070000, 0).empty());
        // "DE W1AW K1ABC": two different calls, the second does not count
        CHECK(none.feed("PSK", "DE W1AW K1ABC K1ABC K1ABC ", 14070000, 0).empty());
        CHECK(none.sightings("K1ABC", 0) == 0);
    }

    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
