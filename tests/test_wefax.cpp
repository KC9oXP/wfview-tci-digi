// Qt-free tests of the WEFAX receiver: APT start, phasing (alignment and
// slant from the pulse spacing), image rows and APT stop, at 120 and 60 LPM,
// with a sound-card clock error and noise.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_wefax.cpp engine/wefaxcore.cpp -o test_wefax

#include "wefaxcore.h"

#include <cmath>
#include <cstdio>
#include <random>
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

static void run(double lpm, int ioc, double ppm, double noise)
{
    const int W = wefax::widthForIoc(ioc);
    const int rows = 120;
    std::vector<uint8_t> img(size_t(W) * rows);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < W; ++x)
        {
            uint8_t v = uint8_t(x * 255 / (W - 1)); // ramp
            if ((x / 100) % 2 == 0 && y > 40 && y < 80)
                v = 255 - v; // stripes
            img[size_t(y) * W + x] = v;
        }
    WefaxEncoder enc;
    enc.start(img, W, lpm, ioc, 20);
    const double fs = 48000.0;
    std::vector<float> audio(size_t(1.0 * fs), 0.0f), block(4800);
    while (!enc.done())
    {
        enc.render(block.data(), block.size(), fs * (1.0 + ppm * 1e-6));
        audio.insert(audio.end(), block.begin(), block.end());
    }
    std::mt19937 rng(7);
    std::normal_distribution<float> nd(0.0f, float(noise));
    for (float &v : audio)
        v = 0.5f * v + nd(rng);

    WefaxDecoder dec;
    dec.setSampleRate(fs);
    dec.setLpm(lpm);
    bool started = false, finished = false;
    for (size_t p = 0; p < audio.size(); p += 2400)
    {
        const WefaxDecoder::Events ev = dec.process(audio.data() + p, std::min<size_t>(2400, audio.size() - p));
        started |= ev.started;
        finished |= ev.finished;
    }
    std::printf(" LPM %.0f IOC %d: started %d finished %d rows %d phasing %d ratio %.6f\n",
                lpm,
                ioc,
                int(started),
                int(finished),
                dec.rows(),
                dec.phasingLines(),
                dec.periodRatio());
    CHECK(started);
    CHECK(finished);
    CHECK(dec.ioc() == ioc);
    CHECK(std::abs(dec.rows() - rows) <= 12); // the stop tone adds a few
    CHECK(dec.phasingLines() >= 12);
    CHECK(std::fabs(dec.periodRatio() - (1.0 + ppm * 1e-6)) < 0.0001);

    // Alignment: best row offset 0..3, mean error over the first image rows.
    double bestErr = 1e9;
    int bestShift = 0;
    const std::vector<uint8_t> &got = dec.image();
    for (int shift = 0; shift < 4; ++shift)
    {
        double e = 0.0;
        long cnt = 0;
        for (int y = 5; y < 100 && y + shift < dec.rows(); ++y)
            for (int x = 20; x < W - 20; ++x)
            {
                e += std::abs(int(got[size_t(y + shift) * W + x]) - int(img[size_t(y) * W + x]));
                ++cnt;
            }
        if (cnt && e / cnt < bestErr)
        {
            bestErr = e / cnt;
            bestShift = shift;
        }
    }
    std::printf("  row shift %d mean error %.1f\n", bestShift, bestErr);
    CHECK(bestErr < 25.0);
}

int main()
{
    run(120.0, 576, 200.0, 0.05);
    run(60.0, 576, -300.0, 0.05);
    run(120.0, 288, 0.0, 0.2);
    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
