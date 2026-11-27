// Qt-free tests of the shared squelch (engine/squelch.h) and of how the RTTY
// and PSK decoders use it: 0 = off, open delay, hang, hysteresis, the dB scale.
// Build: g++ -std=c++17 -O2 -Wall -Iengine tests/test_squelch.cpp engine/pskcore.cpp engine/rttycore.cpp
//        engine/rttytxsource.cpp -o test_squelch

#include "pskcore.h"
#include "rttycore.h"
#include "rttytxsource.h"
#include "squelch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

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

static void testGate()
{
    std::printf("Gate: 0 = off, open delay, hang, hysteresis\n");
    squelch::Gate g;
    g.setThreshold(0.0);
    CHECK(g.off() && g.open() && g.update(-5.0, 0.1));
    g.setThreshold(-1.0);
    CHECK(g.off() && g.open());

    g.setThreshold(10.0);
    g.setTiming({0.3, 0.5, 2.0});
    CHECK(!g.open());
    CHECK(!g.update(12.0, 0.1)); // 0.1 s above: not yet
    CHECK(!g.update(12.0, 0.1));
    CHECK(g.update(12.0, 0.1)); // 0.3 s: open
    CHECK(g.update(9.0, 0.1)); // within the hysteresis: still open
    for (int i = 0; i < 10; ++i)
        g.update(12.0, 0.1); // open for over a second now
    CHECK(g.update(7.0, 0.2)); // below: hang 0.5 s starts
    CHECK(g.update(7.0, 0.2));
    CHECK(!g.update(7.0, 0.2)); // 0.6 s below: closed
    CHECK(!g.update(9.0, 0.1)); // in the hysteresis band but closed: stays closed
    CHECK(std::fabs(g.level() - 9.0) < 1e-12);
    // just opened (a spike): the hang is no longer than it has been open
    CHECK(!g.update(12.0, 0.1) && !g.update(12.0, 0.1) && g.update(12.0, 0.1));
    CHECK(g.update(12.0, 0.1)); // open 0.2 s (counted from the update that opened it)
    CHECK(g.update(7.0, 0.1)); // hang 0.2 s, not 0.5
    CHECK(!g.update(7.0, 0.15));

    // a spike shorter than the open delay never opens it; time above leaks away
    squelch::Gate s;
    s.setThreshold(5.0);
    s.setTiming({0.2, 0.0, 0.0});
    for (int i = 0; i < 50; ++i)
    {
        s.update(i % 4 == 0 ? 6.0 : 1.0, 0.1);
    }
    CHECK(!s.open());
    // hang 0: closes on the first block below
    s.update(6.0, 0.1);
    s.update(6.0, 0.1);
    CHECK(s.open());
    CHECK(!s.update(4.0, 0.1));
    // threshold 0 while open: off (open); back on: starts closed
    s.update(6.0, 0.1);
    s.update(6.0, 0.1);
    s.setThreshold(0.0);
    CHECK(s.open());
    s.setThreshold(5.0);
    CHECK(!s.open());

    CHECK(std::fabs(squelch::ampDb(10.0) - 20.0) < 1e-9 && std::fabs(squelch::powDb(10.0) - 10.0) < 1e-9);
    CHECK(std::isfinite(squelch::ampDb(0.0)) && std::isfinite(squelch::powDb(0.0)));
}

// PSK31 audio: text with idle before and after; optional smooth fade (QSB) to
// 'depth' of the signal over dipLen seconds from dipAt.
static std::vector<float> pskAudio(const std::string &text, double noise, double dipAt, double dipLen, unsigned seed,
                                   double depth = 0.25)
{
    const double fs = 48000.0;
    PskModulator m;
    m.setBaud(psk::baudFor(psk::Bpsk31));
    m.setCarrier(1000.0);
    m.preroll(1.0);
    for (char c : text)
        m.queueChar(c);
    m.tail(0.3);
    std::vector<float> x(size_t(0.37 * fs), 0.0f), b(1024);
    while (!m.idle())
    {
        m.render(b.data(), b.size(), fs);
        x.insert(x.end(), b.begin(), b.end());
    }
    x.insert(x.end(), size_t(0.5 * fs), 0.0f);
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.0f, float(noise));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = double(i) / fs;
        double g = 1.0;
        if (dipLen > 0.0 && t >= dipAt && t < dipAt + dipLen)
        {
            const double s = std::sin(M_PI * (t - dipAt) / dipLen);
            g = 1.0 - (1.0 - depth) * s * s;
        }
        x[i] = float(0.3 * g) * x[i] + nd(rng);
    }
    return x;
}

static std::string pskDecode(const std::vector<float> &x, double sq, bool db, double *maxLevel = nullptr)
{
    PskDemodulator d;
    d.setSampleRate(48000.0);
    d.setBaud(psk::baudFor(psk::Bpsk31));
    d.setCarrier(1000.0);
    d.setAfc(true);
    d.setSquelch(sq, db);
    std::string out;
    double mx = 0.0;
    for (size_t i = 0; i < x.size(); i += 777)
    {
        out += d.process(x.data() + i, std::min<size_t>(777, x.size() - i));
        mx = std::max(mx, d.stats().squelchLevel);
    }
    if (maxLevel)
        *maxLevel = mx;
    return out;
}

static void testPsk()
{
    std::printf("PSK: S/N from quality\n");
    CHECK(psk::snrDbFromQuality(0.0) == 0.0 && psk::snrDbFromQuality(-0.3) == 0.0);
    CHECK(psk::snrDbFromQuality(0.35) < 3.0);
    CHECK(psk::snrDbFromQuality(0.99) > 20.0 && psk::snrDbFromQuality(1.0) == 40.0);
    CHECK(psk::snrDbFromQuality(0.5) < psk::snrDbFromQuality(0.6));
    CHECK(psk::snrDbFromQuality(0.9) < psk::snrDbFromQuality(0.95));

    std::printf("PSK: 10 s of noise prints nothing (quality 0.30, 3 dB)\n");
    std::vector<float> noise(size_t(48000 * 10));
    {
        std::mt19937 rng(9);
        std::normal_distribution<float> nd(0.0f, 0.2f);
        for (float &v : noise)
            v = nd(rng);
    }
    double lvl = 0.0;
    std::string got = pskDecode(noise, 0.30, false);
    std::printf("  quality: [%s]\n", got.c_str());
    CHECK(got.size() <= 1);
    got = pskDecode(noise, 3.0, true, &lvl);
    std::printf("  dB: [%s] highest level %.1f dB\n", got.c_str(), lvl);
    CHECK(got.size() <= 1);
    got = pskDecode(noise, 0.0, true);
    CHECK(got.size() > 10); // off: noise prints

    std::printf("PSK: a 1.5 s fade to -12 dB mid-text, noisy: the same letters as with the squelch off (the "
                "quality dips in the fade; the hang rides it out)\n");
    const std::string text = "The quick brown fox jumps over the lazy dog 0123456789\n";
    int keptQ = 0, keptDb = 0, runs = 0;
    for (unsigned seed : {1u, 2u, 3u, 4u, 5u})
    {
        const std::vector<float> x = pskAudio(text, 0.9, 3.0 + 0.1 * seed, 1.5, seed);
        const std::string q = pskDecode(x, 0.30, false);
        const std::string d = pskDecode(x, 3.0, true);
        const std::string off = pskDecode(x, 0.0, false);
        std::printf("  seed %u: q [%s] dB [%s] off [%s]\n", seed, q.c_str(), d.c_str(), off.c_str());
        // squelch off prints the same, with noise letters before the text
        const size_t t0 = off.find("The q");
        const std::string offText = t0 == std::string::npos ? off : off.substr(t0);
        keptQ += q == offText;
        keptDb += d == offText;
        ++runs;
    }
    CHECK(keptQ == runs);
    CHECK(keptDb == runs);
    {
        const std::vector<float> x = pskAudio(text, 0.05, 0.0, 0.0, 7);
        const std::string full = "The quick brown fox jumps over the lazy dog 0123456789";
        CHECK(pskDecode(x, 0.30, false).find(full) != std::string::npos);
        double mx = 0.0;
        const std::string a = pskDecode(x, 10.0, true, &mx);
        std::printf("  10 dB: [%s] highest level %.1f dB\n", a.c_str(), mx);
        CHECK(a.find(full) != std::string::npos);
        CHECK(pskDecode(x, mx + 1.0, true).empty()); // above the signal: nothing
    }
}

static std::string rttyRun(const std::vector<float> &x, double sq, bool db)
{
    RttyConfig cfg;
    cfg.sampleRate = 48000.0;
    cfg.markHz = 2125.0;
    RttyDemodulator d;
    d.setConfig(cfg);
    d.setSquelch(sq, db);
    std::string out;
    for (size_t i = 0; i + 480 <= x.size(); i += 480)
        out += d.process(x.data() + i, 480);
    return out;
}

static void testRtty()
{
    std::printf("RTTY: noise prints nothing at 0.50 / 8 dB; a weak signal prints at both; 0 = off\n");
    std::vector<float> noise(size_t(48000 * 60));
    std::mt19937 rng(11);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    for (float &v : noise)
        v = nd(rng);
    CHECK(rttyRun(noise, 0.50, false).size() <= 1);
    CHECK(rttyRun(noise, 8.0, true).size() <= 1);
    CHECK(rttyRun(noise, 0.0, true).size() > 20);

    RttyConfig cfg;
    cfg.sampleRate = 48000.0;
    cfg.markHz = 2125.0;
    RttyTxSource tx;
    tx.setConfig(cfg);
    tx.setLevel(0.5f);
    tx.start(0.3);
    const std::string msg = "CQ CQ DE KC9OXP KC9OXP K 599 73 THE QUICK BROWN FOX\n";
    tx.queueText(msg);
    tx.queueTail(0.3);
    std::vector<float> st(4096), y;
    while (!tx.idle())
    {
        tx.render(st.data(), 2048, 2, 48000.0f);
        for (int i = 0; i < 2048; ++i)
            y.push_back(st[size_t(2 * i)]);
    }
    std::mt19937 r2(3);
    std::normal_distribution<float> n2(0.0f, 1.5f);
    for (float &v : y)
        v += n2(r2);
    const std::string q = rttyRun(y, 0.50, false), d = rttyRun(y, 8.0, true);
    std::printf("  quality: %s  dB: %s", q.c_str(), d.c_str());
    CHECK(q.find("CQ CQ DE KC9OXP KC9OXP K 599 73 THE QUICK BROWN FOX") != std::string::npos);
    CHECK(d.find("CQ CQ DE KC9OXP KC9OXP K 599 73 THE QUICK BROWN FOX") != std::string::npos);
}

int main()
{
    testGate();
    testPsk();
    testRtty();
    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
