// Qt-free tests of the SSTV modem: every mode through encoder -> decoder,
// with VIS detection, a sound-card clock error (slant), noise, and a manual
// start without VIS.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_sstv.cpp engine/sstvcore.cpp -o test_sstv

#include "audioresampler.h"
#include "sstvcore.h"

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

// Colour bars over a vertical grey ramp, with a white box: plenty of edges.
static std::vector<uint32_t> testImage(int w, int h)
{
    static const uint32_t bars[8] = {0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000};
    std::vector<uint32_t> img(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint32_t c;
            if (y < h / 2)
                c = bars[x * 8 / w];
            else
            {
                const int g = x * 255 / (w - 1);
                c = (uint32_t(g) << 16) | (uint32_t(g) << 8) | uint32_t(g);
            }
            if (x > w / 3 && x < w / 2 && y > h * 5 / 8 && y < h * 7 / 8)
                c = 0xffffff;
            img[size_t(y) * size_t(w) + size_t(x)] = c;
        }
    return img;
}

// Mean absolute error per channel over the interior (edges blur).
static double meanError(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b, int w, int h)
{
    double e = 0.0;
    long n = 0;
    for (int y = 2; y < h - 2; ++y)
        for (int x = 4; x < w - 4; ++x)
        {
            const uint32_t p = a[size_t(y) * size_t(w) + size_t(x)], q = b[size_t(y) * size_t(w) + size_t(x)];
            for (int s = 0; s < 24; s += 8)
                e += std::abs(int((p >> s) & 0xff) - int((q >> s) & 0xff));
            n += 3;
        }
    return e / double(n);
}

struct Run
{
    int detected = -1;
    bool finished = false;
    double err = 999.0;
    double ratio = 0.0;
    int lines = 0;
};

static Run runMode(int mode, double clockPpm, double noise, bool manual)
{
    const sstv::ModeInfo &m = sstv::info(mode);
    const std::vector<uint32_t> img = testImage(m.width, m.height);
    const double fs = 48000.0;
    // Render at a slightly wrong rate, then call it 48 kHz: the receiver sees
    // lines that are longer or shorter by clockPpm.
    SstvEncoder enc;
    enc.start(mode, img);
    std::vector<float> audio(size_t(0.4 * fs), 0.0f);
    std::vector<float> block(4096);
    const double txRate = fs * (1.0 + clockPpm * 1e-6);
    std::vector<float> rendered;
    while (!enc.done())
    {
        enc.render(block.data(), block.size(), txRate);
        rendered.insert(rendered.end(), block.begin(), block.end());
    }
    audio.insert(audio.end(), rendered.begin(), rendered.end());
    audio.insert(audio.end(), size_t(1.5 * fs), 0.0f);
    std::mt19937 rng(1234 + mode);
    std::normal_distribution<float> nd(0.0f, float(noise));
    for (float &v : audio)
        v = 0.5f * v + nd(rng);

    SstvDecoder dec;
    dec.setSampleRate(fs);
    Run r;
    size_t pos = 0;
    if (manual)
    {
        // Skip the VIS header and start by hand somewhere in the first line.
        pos = size_t((0.4 + 0.910 + 0.11) * fs);
        dec.process(audio.data(), pos);
        dec.startManual(mode);
        r.detected = mode;
    }
    for (; pos < audio.size(); pos += 1000)
    {
        const size_t n = std::min<size_t>(1000, audio.size() - pos);
        const SstvDecoder::Events ev = dec.process(audio.data() + pos, n);
        if (ev.started)
            r.detected = dec.mode();
        if (ev.finished)
        {
            r.finished = true;
            break;
        }
    }
    if (dec.mode() == mode)
    {
        std::vector<uint32_t> got = dec.image();
        for (uint32_t &v : got)
            v &= 0xffffff;
        r.err = meanError(img, got, m.width, m.height);
        r.ratio = dec.periodRatio();
        r.lines = dec.linesDone();
    }
    return r;
}

int main()
{
    std::printf("SSTV loopback, every mode, 300 ppm clock error, light noise\n");
    for (int mode = 0; mode < sstv::ModeCount; ++mode)
    {
        const sstv::ModeInfo &m = sstv::info(mode);
        const Run r = runMode(mode, 300.0, 0.02, false);
        std::printf(" %-10s detected %d finished %d lines %d/%d err %.1f ratio %.6f\n",
                    m.name,
                    r.detected,
                    int(r.finished),
                    r.lines,
                    m.height,
                    r.err,
                    r.ratio);
        CHECK(r.detected == mode);
        CHECK(r.finished);
        CHECK(r.lines == m.height);
        CHECK(r.err < 14.0);
        CHECK(std::fabs(r.ratio - 1.0003) < 0.0001);
    }

    std::printf("Noisy Martin M1 and PD120\n");
    for (int mode : {int(sstv::Martin1), int(sstv::PD120)})
    {
        const Run r = runMode(mode, -150.0, 0.25, false);
        std::printf(" %-10s detected %d err %.1f ratio %.6f\n", sstv::info(mode).name, r.detected, r.err, r.ratio);
        CHECK(r.detected == mode);
        CHECK(r.err < 30.0);
    }

    std::printf("Manual start (no VIS): Scottie S1 and Robot 36\n");
    for (int mode : {int(sstv::Scottie1), int(sstv::Robot36)})
    {
        const Run r = runMode(mode, 0.0, 0.02, true);
        std::printf(" %-10s err %.1f lines %d\n", sstv::info(mode).name, r.err, r.lines);
        CHECK(r.err < 20.0);
    }

    std::printf("VIS table: every mode has a unique code\n");
    for (int a = 0; a < sstv::ModeCount; ++a)
        CHECK(sstv::modeForVis(sstv::info(a).vis) == a);

    std::printf("AudioResampler 48000 -> 8000 -> 48000 keeps a 1 kHz tone\n");
    {
        std::vector<float> x(48000);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = float(std::sin(2 * M_PI * 1000.0 * i / 48000.0));
        AudioResampler down(48000, 8000), up(8000, 48000);
        std::vector<float> y, z;
        down.process(x.data(), x.size(), y);
        up.process(y.data(), y.size(), z);
        double p = 0;
        for (size_t i = 4800; i < 40000; ++i)
            p += double(z[i]) * z[i];
        p /= (40000 - 4800);
        std::printf("  sizes %zu %zu power %.3f\n", y.size(), z.size(), p);
        CHECK(std::abs(long(y.size()) - 8000) <= 2);
        CHECK(std::fabs(p - 0.5) < 0.03);
    }

    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
