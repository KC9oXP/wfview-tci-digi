// Qt-free tests of MT63 (engine/mt63core over Pawel Jalocha's MT63 code):
// loopback for every bandwidth and interleave, sample rates, frequency
// offset, weak signal; the squelch (no junk before or after the text, none on
// noise or on other modes); the transmit echo count and level.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_mt63.cpp engine/mt63core.cpp engine/mt63/dsp.cxx
// engine/mt63/mt63base.cxx engine/oliviacore.cpp engine/pskcore.cpp -o test_mt63

#include "mt63core.h"
#include "oliviacore.h"
#include "pskcore.h"

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

static const std::string kText = "CQ CQ DE KC9OXP KC9OXP EN71AR - THE QUICK BROWN FOX 0123456789\n";

// The transmission, with silence before and after; *sent = characters echoed.
static std::vector<float> transmit(const mt63::Config &c, double fs, size_t *sent, float *peak)
{
    Mt63Modulator m;
    m.setConfig(c);
    m.preroll(0.0);
    for (char ch : kText)
        m.queueChar(ch);
    m.tail(0.0);
    std::vector<float> x(size_t(0.7 * fs), 0.0f), b(1000);
    float pk = 0.0f;
    for (int g = 0; g < 1000000 && !m.idle(); ++g)
    {
        m.render(b.data(), b.size(), fs);
        for (float v : b)
            pk = std::max(pk, std::fabs(v));
        x.insert(x.end(), b.begin(), b.end());
    }
    if (sent)
        *sent = m.unitsSent();
    if (peak)
        *peak = pk;
    x.insert(x.end(), size_t((mt63::delaySeconds(c) + 6.0) * fs), 0.0f);
    return x;
}

static void addNoise(std::vector<float> &x, double gain, double sigma, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, sigma);
    for (float &v : x)
        v = float(v * gain + nd(rng));
}

static std::string receive(const std::vector<float> &x, const mt63::Config &c, double fs, double *snr = nullptr)
{
    Mt63Demodulator d;
    d.setSampleRate(fs);
    d.setConfig(c);
    std::string out;
    double best = -99.0;
    for (size_t i = 0; i < x.size(); i += 960)
    {
        out += d.process(x.data() + i, std::min<size_t>(960, x.size() - i));
        best = std::max(best, d.snrDb());
    }
    if (snr)
        *snr = best;
    return out;
}

int main()
{
    std::printf("=== names and limits ===\n");
    {
        mt63::Config c;
        CHECK(mt63::name(c) == "MT63-1000L");
        c.bandwidth = 500;
        c.longInterleave = false;
        CHECK(mt63::name(c) == "MT63-500S");
        CHECK(std::fabs(mt63::delaySeconds(c) - 6.4) < 1e-9);
        CHECK(mt63::minCentreHz(2000) == 1200.0 && mt63::maxCentreHz(2000) == 2000.0);
    }

    std::printf("\n=== loopback: every mode, 48 kHz, noise, the receiver 15 Hz off ===\n");
    for (int bw : {500, 1000, 2000})
        for (bool lg : {false, true})
        {
            mt63::Config c;
            c.bandwidth = bw;
            c.longInterleave = lg;
            c.centreHz = 1500.0;
            size_t sent = 0;
            float peak = 0.0f;
            std::vector<float> x = transmit(c, 48000.0, &sent, &peak);
            addNoise(x, 0.5, 0.25, unsigned(bw + lg));
            mt63::Config rc = c;
            rc.centreHz = 1515.0;
            double snr = 0.0;
            const std::string out = receive(x, rc, 48000.0, &snr);
            std::printf(
                "  %s: sent %zu, peak %.2f, S/N %.1f dB: [%s]\n", mt63::name(c).c_str(), sent, peak, snr, out.c_str());
            CHECK(out == kText); // nothing before, nothing after
            CHECK(sent == kText.size());
            CHECK(peak > 0.5f && peak <= 1.0f);
        }

    std::printf("\n=== sample rates (MT63-1000L) ===\n");
    for (double fs : {8000.0, 12000.0, 44100.0})
    {
        mt63::Config c;
        std::vector<float> x = transmit(c, fs, nullptr, nullptr);
        addNoise(x, 0.5, 0.25 * std::sqrt(fs / 48000.0), 7);
        const std::string out = receive(x, c, fs);
        std::printf("  %.0f Hz: [%s]\n", fs, out.c_str());
        CHECK(out == kText);
    }

    std::printf("\n=== weaker signal (MT63-1000L, several seeds) ===\n");
    for (unsigned seed : {31u, 32u, 33u})
    {
        mt63::Config c;
        std::vector<float> x = transmit(c, 48000.0, nullptr, nullptr);
        addNoise(x, 0.2, 0.25, seed);
        double snr = 0.0;
        const std::string out = receive(x, c, 48000.0, &snr);
        int wrong = 0;
        for (size_t i = 0; i < std::min(out.size(), kText.size()); ++i)
            wrong += out[i] != kText[i];
        std::printf("  seed %u, S/N %.1f dB, %d wrong: [%s]\n", seed, snr, wrong, out.c_str());
        CHECK(out.size() == kText.size() && wrong <= 2); // a letter or two may be hit
    }

    std::printf("\n=== squelch: nothing on noise, a carrier, PSK31 or Olivia ===\n");
    {
        mt63::Config c;
        std::vector<float> n(size_t(120 * 48000), 0.0f);
        addNoise(n, 0.0, 0.25, 41);
        CHECK(receive(n, c, 48000.0).empty());

        std::vector<float> car(size_t(30 * 48000));
        for (size_t i = 0; i < car.size(); ++i)
            car[i] = 0.3f * float(std::sin(2.0 * M_PI * 1480.0 * double(i) / 48000.0));
        addNoise(car, 1.0, 0.05, 42);
        CHECK(receive(car, c, 48000.0).empty());

        PskModulator p;
        p.setBaud(31.25);
        p.setCarrier(1500.0);
        p.preroll(1.0);
        for (int i = 0; i < 6; ++i)
            for (char ch : kText)
                p.queueChar(ch);
        p.tail(0.5);
        std::vector<float> ps, b(1024);
        while (!p.idle())
        {
            p.render(b.data(), b.size(), 48000.0);
            ps.insert(ps.end(), b.begin(), b.end());
        }
        addNoise(ps, 0.5, 0.05, 43);
        CHECK(receive(ps, c, 48000.0).empty());

        olivia::Config oc;
        oc.centreHz = 1500.0;
        OliviaModulator o;
        o.setConfig(oc);
        o.preroll(1.0);
        for (char ch : kText)
            o.queueChar(ch);
        std::vector<float> ol;
        bool tailed = false;
        for (int g = 0; g < 100000; ++g)
        {
            if (o.idle())
            {
                if (tailed)
                    break;
                o.tail(0);
                tailed = true;
            }
            o.render(b.data(), b.size(), 48000.0);
            ol.insert(ol.end(), b.begin(), b.end());
        }
        addNoise(ol, 0.5, 0.05, 44);
        const std::string out = receive(ol, c, 48000.0);
        std::printf("  Olivia 8/500 into MT63: [%s]\n", out.c_str());
        CHECK(out.empty());
    }

    std::printf("\n=== transmit: echo count, idle, clear ===\n");
    {
        Mt63Modulator m;
        mt63::Config c;
        m.setConfig(c);
        CHECK(m.idle()); // not started
        m.preroll(0.0);
        CHECK(m.queueChar('A') == 1);
        CHECK(m.queueChar('\n') == 2);
        CHECK(m.queueChar('\x01') == 0); // control characters are dropped
        CHECK(!m.idle());
        std::vector<float> b(48000);
        m.render(b.data(), b.size(), 48000.0); // 1 s: inside the opening interleave
        CHECK(m.unitsSent() == 0);
        m.clear();
        CHECK(m.idle() && m.unitsSent() == 0);
    }

    std::printf(g_fail ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
