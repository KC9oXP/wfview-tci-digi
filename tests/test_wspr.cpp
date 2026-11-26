// WSPR engine tests (Qt-free):
//   g++ -std=c++17 -O2 -Iengine tests/test_wspr.cpp engine/wsprcore.cpp engine/wsprd/*.cpp -o test_wspr
#include "audioresampler.h"
#include "wsprcore.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(c))                                                                                                      \
        {                                                                                                              \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
    } while (0)

struct Tx
{
    std::string msg;
    double hz;
    double snrDb; // in 2500 Hz
    double startSec = wspr::kRecordLeadSec + wspr::kTxStartSec; // in the recording
};

// One slot's recording as the app makes it: radio audio at fs, resampled to 12 kHz.
static std::vector<float> slotAudio(const std::vector<Tx> &txs, double fs, unsigned seed)
{
    const size_t n = size_t(wspr::kRecordSec * fs);
    std::vector<float> x(n, 0.0f);
    std::mt19937 rng(seed);
    const double sigma = 0.05;
    std::normal_distribution<double> g(0.0, sigma);
    for (float &v : x)
        v = float(g(rng));
    const double noise2500 = sigma * sigma * 2500.0 / (fs / 2.0);
    for (const Tx &t : txs)
    {
        std::array<std::uint8_t, wspr::kSymbols> sym;
        CHECK(wspr::encode(t.msg, sym));
        wspr::Modulator m;
        m.start(sym, t.hz);
        const double amp = std::sqrt(2.0 * noise2500 * std::pow(10.0, t.snrDb / 10.0));
        std::vector<float> s(size_t(wspr::kTxSeconds * fs) + 10);
        m.render(s.data(), s.size(), fs);
        const size_t off = size_t(t.startSec * fs);
        for (size_t k = 0; k < s.size() && off + k < n; ++k)
            x[off + k] += float(amp * s[k]);
    }
    if (fs == wspr::kAudioRate)
        return x;
    AudioResampler r(fs, wspr::kAudioRate);
    std::vector<float> out;
    r.process(x.data(), x.size(), out);
    return out;
}

static const wspr::Spot *find(const std::vector<wspr::Spot> &v, const std::string &call)
{
    for (const wspr::Spot &s : v)
        if (s.call == call)
            return &s;
    return nullptr;
}

int main()
{
    using namespace wspr;
    // ---- validation
    CHECK(validDbm(0) && validDbm(3) && validDbm(7) && validDbm(37) && validDbm(60));
    CHECK(!validDbm(1) && !validDbm(61) && !validDbm(-3) && !validDbm(35));
    CHECK(nearestDbm(31) == 30 && nearestDbm(35) == 33 && nearestDbm(99) == 60);
    CHECK(standardCall("KC9OXP") && standardCall("kc9oxp") && standardCall("K1ABC") && standardCall("G4X"));
    CHECK(standardCall("2E0ABC") && standardCall("W1AW"));
    CHECK(!standardCall("KC9OXPX") && !standardCall("ABCDEF") && !standardCall("K1A2B") && !standardCall("PJ4/K1ABC"));
    CHECK(compoundCall("PJ4/K1ABC") && compoundCall("K1ABC/P") && compoundCall("K1ABC/12"));
    CHECK(!compoundCall("K1ABC") && !compoundCall("K1ABC/PP") && !compoundCall("ABCD/K1ABC") && !compoundCall("A/B/C"));
    CHECK(validGrid("EN71") && validGrid("en71ar") && !validGrid("EN7") && !validGrid("ZZ99") && !validGrid("EN71AZ"));

    std::string err;
    std::vector<std::string> b = beaconMessages("kc9oxp", "en71", 30, &err);
    CHECK(b.size() == 1 && b[0] == "KC9OXP EN71 30" && err.empty());
    b = beaconMessages("KC9OXP", "EN71AR", 37, &err);
    CHECK(b.size() == 2 && b[0] == "KC9OXP EN71 37" && b[1] == "<KC9OXP> EN71AR 37");
    b = beaconMessages("PJ4/K1ABC", "FK52UD", 23, &err);
    CHECK(b.size() == 2 && b[0] == "PJ4/K1ABC 23" && b[1] == "<PJ4/K1ABC> FK52UD 23");
    CHECK(beaconMessages("KC9OXP", "EN71", 31, &err).empty() && !err.empty());
    CHECK(beaconMessages("TOOLONG1", "EN71", 30, &err).empty() && !err.empty());

    std::array<std::uint8_t, kSymbols> sym {}, sym2 {};
    CHECK(encode("K1ABC FN42 37", sym));
    CHECK(encode("k1abc fn42 37", sym2) && sym == sym2);
    // Sync vector: the low bit of every symbol is the fixed pseudo-random pattern.
    const int pr3[8] = {1, 1, 0, 0, 0, 0, 0, 0};
    for (int k = 0; k < 8; ++k)
        CHECK((sym[size_t(k)] & 1) == pr3[k]);

    // ---- modulator
    {
        Modulator m;
        m.start(sym, 1500.0);
        std::vector<float> s(size_t(12000 * 112));
        m.render(s.data(), s.size(), 12000.0);
        CHECK(!m.active());
        const size_t end = size_t(kTxSeconds * 12000.0);
        CHECK(std::fabs(s[end - 2000]) <= 1.0f && s[end + 100] == 0.0f);
        double e = 0;
        for (size_t k = 1000; k < 2000; ++k)
            e += double(s[k]) * s[k];
        CHECK(std::fabs(e / 1000.0 - 0.5) < 0.02);
    }

    // ---- receive, 48 kHz radio audio through the app's resampler
    Decoder dec;
    const std::int64_t dial = 14095600;
    {
        auto t0 = std::chrono::steady_clock::now();
        std::vector<float> a = slotAudio({{"K1ABC FN42 37", 1460.0, -20.0}, {"KC9OXP EN71 30", 1530.0, -24.0}}, 48000.0, 1);
        std::vector<float> i, q;
        toBaseband(a, i, q);
        std::vector<Spot> sp = dec.decode(i, q, dial);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("slot 1: %zu spots in %.1f s\n", sp.size(), sec);
        for (const Spot &s : sp)
            std::printf("  %-22s snr %5.1f dt %5.2f drift %4.1f audio %7.1f rf %lld\n", s.message.c_str(), s.snr, s.dt,
                        s.drift, s.audioHz, (long long)s.rfHz);
        const Spot *a1 = find(sp, "K1ABC"), *a2 = find(sp, "KC9OXP");
        CHECK(a1 && a2);
        if (a1)
        {
            CHECK(a1->grid == "FN42" && a1->dbm == 37);
            CHECK(std::fabs(a1->audioHz - 1460.0) < 1.5);
            CHECK(std::fabs(a1->snr - -20.0) < 3.0);
            CHECK(std::fabs(a1->dt) < 0.5);
            CHECK(a1->rfHz == dial + std::llround(a1->audioHz));
        }
        if (a2)
        {
            CHECK(a2->grid == "EN71" && a2->dbm == 30);
            CHECK(std::fabs(a2->audioHz - 1530.0) < 1.5);
            CHECK(std::fabs(a2->snr - -24.0) < 3.0);
        }
        // ---- spectrum + free offsets
        WindowSpectrum ws = windowSpectrum(i, q);
        CHECK(!ws.db.empty() && std::fabs(ws.binHz - 375.0 / 512.0) < 1e-9);
        FreeFinder ff;
        CHECK(ff.suggest(100).size() == 3); // nothing heard: anywhere is clear
        // An undecodable strong-ish carrier at 1580 Hz shows only as energy.
        ff.addSlot(100, sp, ws);
        std::vector<FreeFinder::Busy> busy = ff.busy(100);
        CHECK(busy.size() >= 2);
        bool saw1460 = false, saw1530 = false;
        for (const FreeFinder::Busy &x : busy)
        {
            saw1460 = saw1460 || (x.lowHz < 1460 && x.highHz > 1460 && x.decoded);
            saw1530 = saw1530 || (x.lowHz < 1530 && x.highHz > 1530 && x.decoded);
        }
        CHECK(saw1460 && saw1530);
        CHECK(ff.clearance(100, 1460.0) == 0.0 && ff.clearance(100, 1531.0) == 0.0);
        std::vector<FreeFinder::Pick> picks = ff.suggest(100, 3);
        CHECK(picks.size() == 3);
        for (const FreeFinder::Pick &p : picks)
        {
            std::printf("  free: %d Hz (clear by %.1f Hz)\n", p.hz, p.clearanceHz);
            CHECK(std::fabs(p.hz - 1460.0) > 8.0 && std::fabs(p.hz - 1530.0) > 8.0);
            CHECK(p.hz >= 1410 && p.hz <= 1590 && p.clearanceHz > 0.0);
        }
        CHECK(ff.slotsSeen(100) == 1 && ff.slotsSeen(100 + FreeFinder::kKeepSlots) == 0);
        CHECK(ff.busy(100 + FreeFinder::kKeepSlots).empty()); // forgotten after 20 minutes
    }
    // Energy only (no decode): a tone at 1580 Hz is marked busy, not decoded.
    {
        std::vector<float> a = slotAudio({}, 12000.0, 7);
        for (size_t k = 0; k < a.size(); ++k)
            a[k] += float(0.01 * std::sin(2.0 * M_PI * 1580.0 * double(k) / 12000.0));
        std::vector<float> i, q;
        toBaseband(a, i, q);
        FreeFinder ff;
        ff.addSlot(5, {}, windowSpectrum(i, q));
        std::vector<FreeFinder::Busy> busy = ff.busy(5);
        CHECK(busy.size() == 1);
        if (!busy.empty())
        {
            CHECK(!busy[0].decoded && busy[0].lowHz < 1580 && busy[0].highHz > 1580);
            CHECK(busy[0].highHz - busy[0].lowHz < 12.0);
        }
        CHECK(ff.clearance(5, 1500.0) > 60.0);
    }

    // ---- Type 3 resolves with a call heard before; Type 2 decodes
    {
        std::vector<float> a = slotAudio({{"<KC9OXP> EN71AR 37", 1500.0, -18.0}, {"PJ4/K1ABC 23", 1420.0, -18.0}},
                                         12000.0, 3);
        std::vector<float> i, q;
        toBaseband(a, i, q);
        std::vector<Spot> sp = dec.decode(i, q, dial);
        for (const Spot &s : sp)
            std::printf("  %-22s call %-10s grid %-6s dbm %d\n", s.message.c_str(), s.call.c_str(), s.grid.c_str(), s.dbm);
        const Spot *t3 = find(sp, "KC9OXP");
        CHECK(t3 && t3->grid == "EN71AR" && t3->dbm == 37);
        const Spot *t2 = find(sp, "PJ4/K1ABC");
        CHECK(t2 && t2->grid.empty() && t2->dbm == 23);
        // A fresh decoder does not know the hash yet.
        Decoder fresh;
        std::vector<Spot> sp2 = fresh.decode(i, q, dial);
        CHECK(!find(sp2, "KC9OXP") && find(sp2, "<...>"));
        // Hashes saved and loaded.
        const std::string h = dec.saveHashes();
        CHECK(h.find("KC9OXP") != std::string::npos);
        Decoder back;
        back.loadHashes(h + "123 BOGUS EN00\n");
        CHECK(back.knownCalls() == dec.knownCalls());
        std::vector<Spot> sp3 = back.decode(i, q, dial);
        CHECK(find(sp3, "KC9OXP"));
    }

    // ---- weak: -26 dB still decodes, pure noise gives nothing
    {
        Decoder d;
        std::vector<float> a = slotAudio({{"W1AW FN31 40", 1510.0, -26.0}}, 12000.0, 11);
        std::vector<float> i, q;
        toBaseband(a, i, q);
        std::vector<Spot> sp = d.decode(i, q, dial);
        CHECK(find(sp, "W1AW"));
        std::vector<float> n = slotAudio({}, 12000.0, 12);
        toBaseband(n, i, q);
        CHECK(d.decode(i, q, dial).empty());
    }
    // ---- timing: a transmission 1.5 s late shows as dt ~ +1.5
    {
        Decoder d;
        Tx t {"K1ABC FN42 37", 1480.0, -15.0};
        t.startSec += 1.5;
        std::vector<float> a = slotAudio({t}, 12000.0, 21);
        std::vector<float> i, q;
        toBaseband(a, i, q);
        std::vector<Spot> sp = d.decode(i, q, dial);
        const Spot *s = find(sp, "K1ABC");
        CHECK(s && std::fabs(s->dt - 1.5) < 0.4);
    }

    // ---- bands and hopping
    CHECK(bandIndex("20 m") >= 0 && bands()[size_t(bandIndex("20 m"))].dialHz == 14095600);
    CHECK(bandForDial(14095600) == bandIndex("20 m") && bandForDial(14074000) == -1);
    CHECK(coordinatedBand(0) == bandIndex("160 m") && coordinatedBand(1) == bandIndex("160 m"));
    CHECK(coordinatedBand(2) == bandIndex("80 m") && coordinatedBand(10) == bandIndex("20 m"));
    CHECK(coordinatedBand(18) == bandIndex("10 m") && coordinatedBand(20) == bandIndex("160 m"));
    CHECK(coordinatedBand(58) == bandIndex("10 m"));
    {
        HopPlan h;
        h.rx.assign(bands().size(), false);
        h.rx[size_t(bandIndex("40 m"))] = h.rx[size_t(bandIndex("20 m"))] = true;
        CHECK(h.bandFor(10) == -1); // off
        h.mode = HopPlan::Coordinated;
        CHECK(h.bandFor(10) == bandIndex("20 m") && h.bandFor(6) == bandIndex("40 m") && h.bandFor(8) == -1);
        CHECK(h.bandFor(60 + 10) == bandIndex("20 m"));
        h.mode = HopPlan::Rotate;
        CHECK(h.bandFor(0) == bandIndex("40 m") && h.bandFor(2) == bandIndex("20 m") && h.bandFor(4) == bandIndex("40 m"));
    }

    // ---- TX percentage
    for (int pct : {10, 20, 33, 50})
    {
        TxScheduler s;
        s.setPercent(pct);
        std::mt19937 rng(pct);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        int tx = 0, gapMax = 0, last = -10;
        bool twoInRow = false;
        for (int k = 0; k < 20000; ++k)
            if (s.decide(k, u(rng)))
            {
                ++tx;
                twoInRow = twoInRow || last == k - 1;
                if (last >= 0 && k > last)
                    gapMax = std::max(gapMax, k - last);
                last = k;
            }
        const double got = 100.0 * tx / 20000.0;
        std::printf("TX %d%%: %.1f%%, longest gap %d slots\n", pct, got, gapMax);
        CHECK(std::fabs(got - pct) < pct * 0.15 + 1.0);
        CHECK(!twoInRow);
        CHECK(gapMax <= int(std::ceil(200.0 / pct)));
    }
    {
        TxScheduler s;
        s.setPercent(0);
        CHECK(!s.decide(1, 0.0));
    }

    // ---- live waterfall line: a tone at 1530 Hz peaks within a bin of 1530
    {
        std::vector<float> a(24000);
        std::mt19937 rng(5);
        std::normal_distribution<double> g(0.0, 0.05);
        for (size_t k = 0; k < a.size(); ++k)
            a[k] = float(g(rng) + 0.02 * std::sin(2.0 * M_PI * 1530.0 * double(k) / 12000.0));
        WindowSpectrum l = liveLine(a.data(), a.size());
        CHECK(!l.db.empty());
        size_t best = 0;
        for (size_t k = 0; k < l.db.size(); ++k)
            if (l.db[k] > l.db[best])
                best = k;
        const double hz = l.firstHz + double(best) * l.binHz;
        std::printf("live line peak %.1f Hz\n", hz);
        CHECK(std::fabs(hz - 1530.0) < 1.5);
        CHECK(liveLine(a.data(), 1000).db.empty());
    }

    // ---- measured power
    {
        PowerMeter pm;
        double w = 0;
        std::string why;
        CHECK(!pm.result(w, &why) && !why.empty());
        for (int k = 0; k < 40; ++k)
            pm.add(k < 6 ? 0.0 : 5.0 + 0.1 * (k % 3), k * 0.5); // nothing for the first 3 s, then ~5 W
        CHECK(pm.result(w, &why) && std::fabs(w - 5.1) < 0.15);
        CHECK(dbmForWatts(w, 0.0) == 37); // 37.1 dBm
        CHECK(dbmForWatts(10.0, 0.0) == 40 && dbmForWatts(10.0, 1.0) == 40 && dbmForWatts(10.0, 2.0) == 37);
        CHECK(dbmForWatts(100.0, 0.0) == 50 && dbmForWatts(0.001, 0.0) == 0 && dbmForWatts(5000.0, 0.0) == 60);
        CHECK(dbmForWatts(0.2, 0.0) == 23);
        PowerMeter jumpy;
        for (int k = 0; k < 40; ++k)
            jumpy.add(k % 2 ? 2.0 : 8.0, 4.0 + k);
        CHECK(!jumpy.result(w, &why));
        PowerMeter none;
        for (int k = 0; k < 40; ++k)
            none.add(0.0, 4.0 + k);
        CHECK(!none.result(w, &why));
    }

    // ---- WSPRnet
    {
        Spot s;
        s.call = "PJ4/K1ABC";
        s.grid = "";
        s.dbm = 23;
        s.snr = -21.4;
        s.dt = 0.34;
        s.drift = -1;
        s.rfHz = 14097063;
        const std::string q = wsprnetSpotQuery("kc9oxp", "EN71", 14095600, s, 261126, 412, "TCIDigi-0.9.17");
        std::printf("%s\n", q.c_str());
        CHECK(q == "function=wspr&rcall=KC9OXP&rgrid=EN71&rqrg=14.095600&date=261126&time=0412&sig=-21&dt=0.3"
                   "&drift=-1&tqrg=14.097063&tcall=PJ4%2FK1ABC&tgrid=&dbm=23&version=TCIDigi-0.9.17&mode=2");
        const std::string st = wsprnetStatusQuery("KC9OXP", "EN71", 14095600, 20, 14095600, 30, "TCIDigi-0.9.17");
        CHECK(st == "function=wsprstat&rcall=KC9OXP&rgrid=EN71&rqrg=14.095600&tpct=20&tqrg=14.095600&dbm=30"
                    "&version=TCIDigi-0.9.17&mode=2");
    }

    if (g_fail)
    {
        std::printf("%d FAILED\n", g_fail);
        return 1;
    }
    std::printf("ALL PASSED\n");
    return 0;
}
