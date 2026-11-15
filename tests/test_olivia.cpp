// Qt-free tests of Olivia / Contestia: modulator -> demodulator through the
// 48 kHz <-> 8 kHz resamplers, with noise and a frequency offset.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_olivia.cpp engine/oliviacore.cpp -o test_olivia

#include "oliviacore.h"

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

static std::string run(olivia::Config c,
                       const std::string &text,
                       double rxOffset,
                       double noise,
                       size_t *echoOut,
                       double preroll = 0.5,
                       double leadNoiseSec = 0.5)
{
    const double fs = 48000.0;
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
    std::mt19937 rng(5);
    std::normal_distribution<float> nd(0.0f, float(noise));
    for (float &v : audio)
        v = 0.3f * v + nd(rng);

    OliviaDemodulator dem;
    dem.setSampleRate(fs);
    olivia::Config rc = c;
    rc.centreHz = c.centreHz + rxOffset;
    dem.setConfig(rc);
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

    std::printf("Squelch: 20 s of band noise prints nothing at squelch 5\n");
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

    std::printf("  Olivia 8/500 %.2f chars/s\n", olivia::charsPerSecond(olivia::Config {}));
    CHECK(olivia::charsPerSecond(olivia::Config {}) > 1.0 && olivia::charsPerSecond(olivia::Config {}) < 3.5);
    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
