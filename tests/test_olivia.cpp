// Qt-free tests of Olivia / Contestia: modulator -> demodulator through the
// 48 kHz <-> 8 kHz resamplers, with noise and a frequency offset.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_olivia.cpp engine/oliviacore.cpp -o test_olivia

#include "oliviacore.h"

#include <cmath>
#include <cstdlib>
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

static std::string run(olivia::Config c,
                       const std::string &text,
                       double rxOffset,
                       double noise,
                       size_t *echoOut,
                       double preroll = 0.5,
                       double leadNoiseSec = 0.5,
                       unsigned seed = 5,
                       double fadeDb = 0.0,
                       double squelch = 6.0,
                       bool squelchDb = false)
{
    const double fs = 48000.0;
    std::srand(seed); // the library's transmitter picks random tone phases with rand()
    OliviaModulator mod;
    mod.setConfig(c);
    mod.preroll(preroll);
    for (char ch : text)
        mod.queueChar(ch);
    std::vector<float> audio(size_t(leadNoiseSec * fs), 0.0f), block(960);
    bool tailed = false;
    int guard = 0;
    while (guard++ < 200000)
    {
        if (!tailed && mod.idle())
        {
            mod.tail(0.0);
            tailed = true;
        }
        else if (tailed && mod.idle())
            break;
        mod.render(block.data(), block.size(), fs);
        audio.insert(audio.end(), block.begin(), block.end());
    }
    if (echoOut)
        *echoOut = mod.unitsSent();
    audio.insert(audio.end(), size_t(12.0 * fs), 0.0f);
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.0f, float(noise));
    for (size_t i = 0; i < audio.size(); ++i)
    {
        // QSB: the signal fades by fadeDb and back every 8 s
        const double t = double(i) / fs;
        const double g = std::pow(10.0, -fadeDb / 20.0 * (0.5 - 0.5 * std::cos(2.0 * M_PI * t / 8.0)));
        audio[i] = float(0.3 * g) * audio[i] + nd(rng);
    }

    OliviaDemodulator dem;
    dem.setSampleRate(fs);
    olivia::Config rc = c;
    rc.centreHz = c.centreHz + rxOffset;
    dem.setConfig(rc);
    dem.setSquelch(squelch, squelchDb);
    std::string out;
    for (size_t p = 0; p < audio.size(); p += 960)
        out += dem.process(audio.data() + p, std::min<size_t>(960, audio.size() - p));
    std::printf("  %s: %.1f s, S/N %.1f, offset %.1f Hz: [%s]\n",
                olivia::name(c).c_str(),
                audio.size() / fs,
                dem.snr(),
                dem.freqOffsetHz(),
                out.c_str());
    return out;
}

int main()
{
    const std::string msg = "CQ CQ de KC9OXP KC9OXP pse k\n";
    olivia::Config c;
    c.centreHz = 1500;
    size_t echo = 0;
    std::string got = run(c, msg, 0.0, 0.05, &echo);
    CHECK(got.find("CQ CQ de KC9OXP KC9OXP pse k") != std::string::npos);
    CHECK(got.size() <= msg.size() + 1); // no junk after the transmission (squelch)
    CHECK(echo == msg.size() + 1); // '\n' goes out as CR LF

    std::printf("Olivia 8/500, 20 Hz mistuned, noisy (in-band S/N about 0 dB)\n");
    got = run(c, msg, 20.0, 0.35, nullptr);
    CHECK(got.find("KC9OXP") != std::string::npos);

    c.tones = 32;
    c.bandwidth = 1000;
    got = run(c, msg, 0.0, 0.1, nullptr);
    CHECK(got.find("CQ CQ de KC9OXP") != std::string::npos);

    c.tones = 4;
    c.bandwidth = 250;
    c.contestia = true;
    c.centreHz = 1000;
    got = run(c, msg, 0.0, 0.05, nullptr);
    CHECK(got.find("CQ CQ DE KC9OXP") != std::string::npos);

    std::printf("Lead-in: the first letter must arrive (app preroll vs none)\n");
    for (bool contestia : {false, true})
    {
        olivia::Config lc;
        lc.centreHz = 1500;
        lc.contestia = contestia;
        lc.tones = contestia ? 4 : 8;
        lc.bandwidth = contestia ? 250 : 500;
        const std::string none = run(lc, "LETS TEST FLDIGI\n", 0.0, 0.08, nullptr, 0.0);
        const std::string with = run(lc, "LETS TEST FLDIGI\n", 0.0, 0.08, nullptr, olivia::prerollSeconds(lc));
        std::printf("  preroll %.2f s\n", olivia::prerollSeconds(lc));
        CHECK(with.find("LETS TEST FLDIGI") != std::string::npos);
        (void)none;
    }

    std::printf("Start of a signal: no junk before the text (several noise seeds; they differ by compiler)\n");
    {
        olivia::Config sc;
        sc.centreHz = 1500;
        int clean = 0, runs = 0;
        for (unsigned seed : {1u, 2u, 7u, 8u, 17u, 31u})
        {
            const std::string o
                = run(sc, "LETS TEST FLDIGI\n", 0.0, 0.15, nullptr, olivia::prerollSeconds(sc), 0.2, seed);
            clean += o.rfind("LETS TEST FLDIGI", 0) == 0;
            ++runs;
        }
        CHECK(clean == runs);
    }

    std::printf("Squelch: 20 s of band noise prints nothing at squelch 5 (and the default 6)\n");
    {
        const double fs = 48000.0;
        std::vector<float> audio(size_t(20 * fs));
        std::mt19937 rng(9);
        std::normal_distribution<float> nd(0.0f, 0.2f);
        for (float &v : audio)
            v = nd(rng);
        for (bool contestia : {false, true})
        {
            OliviaDemodulator dem;
            dem.setSampleRate(fs);
            olivia::Config nc;
            nc.contestia = contestia;
            nc.tones = contestia ? 4 : 8;
            nc.bandwidth = contestia ? 250 : 500;
            dem.setConfig(nc);
            dem.setSquelch(5.0);
            std::string out;
            for (size_t p = 0; p < audio.size(); p += 960)
                out += dem.process(audio.data() + p, 960);
            std::printf("  %s noise -> [%s]\n", olivia::name(nc).c_str(), out.c_str());
            CHECK(out.size() <= 1);
        }
    }

    std::printf("Squelch: an RTTY station (2125/2295 Hz, 45.45 baud) inside an Olivia 8/500 window at 2003 Hz "
                "prints nothing at squelch 6 (or 15.5 dB, the defaults)\n");
    {
        const double fs = 48000.0;
        std::vector<float> audio(size_t(60 * fs));
        std::mt19937 rng(3);
        std::normal_distribution<float> nd(0.0f, 0.05f);
        std::uniform_int_distribution<int> bit(0, 1);
        const size_t bitLen = size_t(fs / 45.45);
        double ph = 0.0;
        bool mark = true;
        for (size_t i = 0; i < audio.size(); ++i)
        {
            if (i % bitLen == 0)
                mark = bit(rng) != 0;
            ph += 2.0 * M_PI * (mark ? 2125.0 : 2295.0) / fs;
            audio[i] = float(0.3 * std::sin(ph)) + nd(rng);
        }
        for (bool db : {false, true})
        {
            OliviaDemodulator dem;
            dem.setSampleRate(fs);
            olivia::Config ic;
            ic.centreHz = 2003.0;
            dem.setConfig(ic);
            dem.setSquelch(db ? 15.5 : 6.0, db);
            std::string out;
            for (size_t p = 0; p + 960 <= audio.size(); p += 960)
                out += dem.process(audio.data() + p, 960);
            std::printf("  %s: [%s] (S/N %.1f)\n", db ? "15.5 dB" : "6.0", out.c_str(), dem.snr());
            CHECK(out.size() <= 1);
        }
    }

    std::printf("QSB: a fade of 10-15 dB every 8 s, noisy: the text through the fades prints as with the squelch "
                "off, and nothing after it\n");
    {
        olivia::Config fc;
        fc.centreHz = 1500;
        const std::string longMsg = "CQ CQ CQ de KC9OXP KC9OXP KC9OXP EN71 pse k\n";
        for (double fade : {10.0, 15.0})
        {
            const double pre = olivia::prerollSeconds(fc);
            const std::string sq = run(fc, longMsg, 0.0, 0.9, nullptr, pre, 0.5, 5, fade);
            const std::string off = run(fc, longMsg, 0.0, 0.9, nullptr, pre, 0.5, 5, fade, 0.0);
            // Squelch off prints the same decode with noise before and after it.
            const std::string body = sq.substr(0, sq.size() > 0 && sq.back() == '\n' ? sq.size() - 1 : sq.size());
            CHECK(sq.size() > 25);
            CHECK(off.find(body) != std::string::npos);
            CHECK(sq.find("EN71 pse k") != std::string::npos);
            CHECK(sq.size() <= longMsg.size() + 1);
        }
    }

    std::printf("Squelch in dB (15.5 dB, about 6) and squelch 0 (off: noise prints)\n");
    {
        olivia::Config dc;
        dc.centreHz = 1500;
        const std::string o = run(dc, msg, 0.0, 0.05, nullptr, 0.5, 0.5, 5, 0.0, 15.5, true);
        CHECK(o.find("CQ CQ de KC9OXP KC9OXP pse k") != std::string::npos);
        CHECK(o.size() <= msg.size() + 1);
        const std::string off = run(dc, msg, 0.0, 0.05, nullptr, 0.5, 0.5, 5, 0.0, 0.0, false);
        CHECK(off.find("CQ CQ de KC9OXP KC9OXP pse k") != std::string::npos);
        CHECK(off.size() > msg.size() + 4); // junk from the noise after the text
        CHECK(std::fabs(OliviaDemodulator::squelchDbOf(5.0) - 13.98) < 0.01);
    }

    std::printf("  Olivia 8/500 %.2f chars/s\n", olivia::charsPerSecond(olivia::Config {}));
    CHECK(olivia::charsPerSecond(olivia::Config {}) > 1.0 && olivia::charsPerSecond(olivia::Config {}) < 3.5);
    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
