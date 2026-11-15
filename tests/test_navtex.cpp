// Qt-free tests of the NAVTEX / SITOR-B receiver: encoder -> decoder with
// noise, a frequency offset and a sample-rate offset.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_navtex.cpp engine/navtexcore.cpp -o test_navtex

#include "navtexcore.h"

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

static std::string run(const std::string &text, double txCentre, double rxCentre, double noise, double fsTx)
{
    NavtexEncoder enc;
    enc.setCentreHz(txCentre);
    enc.start(text, 4.0);
    const double fs = 48000.0;
    std::vector<float> audio(size_t(0.7 * fs), 0.0f), block(1000);
    while (!enc.done())
    {
        enc.render(block.data(), block.size(), fsTx);
        audio.insert(audio.end(), block.begin(), block.end());
    }
    audio.insert(audio.end(), size_t(0.5 * fs), 0.0f);
    std::mt19937 rng(99);
    std::normal_distribution<float> nd(0.0f, float(noise));
    for (float &v : audio)
        v = 0.3f * v + nd(rng);
    NavtexDecoder dec;
    dec.setSampleRate(fs);
    dec.setCentreHz(rxCentre);
    std::string out;
    for (size_t p = 0; p < audio.size(); p += 960)
        out += dec.process(audio.data() + p, std::min<size_t>(960, audio.size() - p));
    return out;
}

int main()
{
    const std::string msg = "ZCZC EA01\nSECURITE. GALE WARNING 8, WINDS 35 KT, SEAS 4.5 M (TEST 123).\nNNNN\n";
    std::printf("Clean\n");
    std::string got = run(msg, 1000, 1000, 0.01, 48000);
    std::printf("  [%s]\n", got.c_str());
    CHECK(got.find("GALE WARNING 8, WINDS 35 KT, SEAS 4.5 M (TEST 123).") != std::string::npos);
    CHECK(got.find("NNNN") != std::string::npos);

    std::printf("Noisy, 6 Hz off, 100 ppm clock\n");
    got = run(msg, 1006, 1000, 0.25, 48000 * 1.0001);
    std::printf("  [%s]\n", got.c_str());
    CHECK(got.find("WINDS 35 KT") != std::string::npos);

    std::printf("Code table round trip: every printable character\n");
    {
        bool figs = false;
        const std::string all = "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789-?:()./,'";
        const std::string codes = navtex::encode(all, figs);
        CHECK(codes.size() >= all.size());
    }
    std::printf("AMTOR FEC transmitter (typed text, idle, end of emission) -> decoder\n");
    {
        AmtorFecModulator mod;
        mod.setCentreHz(1500);
        mod.preroll(3.0);
        const double fs = 48000.0;
        std::vector<float> audio(size_t(0.5 * fs), 0.0f), block(480);
        const std::string first = "CQ CQ DE KC9OXP ";
        size_t lastCount = 0;
        for (char c : first)
            lastCount = mod.queueChar(c);
        CHECK(lastCount == first.size());
        CHECK(!mod.idle());
        int blocks = 0;
        while (!mod.idle() && blocks < 4000)
        {
            mod.render(block.data(), block.size(), fs);
            audio.insert(audio.end(), block.begin(), block.end());
            ++blocks;
        }
        CHECK(mod.unitsSent() == first.size());
        // 1.5 s of idle (beta) between words, as when typing
        for (int i = 0; i < 150; ++i)
        {
            mod.render(block.data(), block.size(), fs);
            audio.insert(audio.end(), block.begin(), block.end());
        }
        for (char c : std::string("AMTOR FEC TEST 73 (1/2).\n"))
            mod.queueChar(c);
        mod.tail(0.0);
        blocks = 0;
        while (!mod.idle() && blocks < 6000)
        {
            mod.render(block.data(), block.size(), fs);
            audio.insert(audio.end(), block.begin(), block.end());
            ++blocks;
        }
        CHECK(mod.idle());
        audio.insert(audio.end(), size_t(0.5 * fs), 0.0f);
        std::mt19937 rng(7);
        std::normal_distribution<float> nd(0.0f, 0.15f);
        for (float &v : audio)
            v = 0.3f * v + nd(rng);
        NavtexDecoder dec;
        dec.setSampleRate(fs);
        dec.setCentreHz(1500);
        std::string out;
        for (size_t p = 0; p < audio.size(); p += 960)
            out += dec.process(audio.data() + p, std::min<size_t>(960, audio.size() - p));
        std::printf("  [%s]\n", out.c_str());
        CHECK(out.find("CQ CQ DE KC9OXP AMTOR FEC TEST 73 (1/2).") != std::string::npos);
    }
    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
