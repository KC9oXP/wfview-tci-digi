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

// ---------------------------------------------------------------------------
// Reference tests: the transmitted tones measured directly (zero crossings),
// against the published mode timings and the 1500 + 800 * v / 255 Hz pixel
// scale, independently of the app's decoder.
// ---------------------------------------------------------------------------

static std::vector<float> encodeAll(int mode, const std::vector<uint32_t> &img, double fs, double offsetHz = 0.0)
{
    SstvEncoder enc;
    enc.setOffsetHz(offsetHz);
    enc.start(mode, img);
    std::vector<float> out, block(4800);
    while (!enc.done())
    {
        enc.render(block.data(), block.size(), fs);
        out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}

// Mean frequency in [t0, t1) seconds from interpolated rising zero crossings.
static double measureHz(const std::vector<float> &x, double fs, double t0, double t1)
{
    const size_t a = size_t(std::max(0.0, t0 * fs)), b = std::min(x.size() - 1, size_t(t1 * fs));
    double first = -1, last = -1;
    int cycles = -1;
    for (size_t i = a; i < b; ++i)
        if (x[i] < 0.0f && x[i + 1] >= 0.0f)
        {
            const double t = double(i) + double(-x[i]) / double(x[i + 1] - x[i]);
            if (first < 0)
                first = t;
            last = t;
            ++cycles;
        }
    if (cycles < 1)
        return 0.0;
    return cycles * fs / (last - first);
}

static double pixHz(int v)
{
    return 1500.0 + 800.0 * v / 255.0;
}

static void referenceTests()
{
    const double fs = 48000.0;
    std::printf("Reference: VIS header of Martin M1 (code 44 = 0101100, even parity)\n");
    {
        const std::vector<uint32_t> img(320 * 256, 0x808080);
        const std::vector<float> a = encodeAll(sstv::Martin1, img, fs);
        CHECK(std::fabs(measureHz(a, fs, 0.010, 0.290) - 1900.0) < 2.0); // leader
        CHECK(std::fabs(measureHz(a, fs, 0.300, 0.310) - 1200.0) < 25.0); // break
        CHECK(std::fabs(measureHz(a, fs, 0.320, 0.600) - 1900.0) < 2.0); // leader
        CHECK(std::fabs(measureHz(a, fs, 0.612, 0.638) - 1200.0) < 8.0); // start bit
        const int bits[8] = {0, 0, 1, 1, 0, 1, 0, 1}; // LSB first, then parity
        for (int j = 0; j < 8; ++j)
        {
            const double f = measureHz(a, fs, 0.640 + 0.03 * j + 0.003, 0.640 + 0.03 * (j + 1) - 0.003);
            std::printf("  bit %d: %.1f Hz\n", j, f);
            CHECK(std::fabs(f - (bits[j] ? 1100.0 : 1300.0)) < 8.0);
        }
        CHECK(std::fabs(measureHz(a, fs, 0.882, 0.908) - 1200.0) < 8.0); // stop bit
        // Whole transmission: 910 ms header + 256 lines of 446.446 ms.
        const double total = double(a.size()) / fs;
        std::printf("  length %.3f s (expected %.3f)\n", total, 0.910 + 256 * 0.446446);
        CHECK(std::fabs(total - (0.910 + 256 * 0.446446)) < 0.11);
    }

    std::printf(
        "Reference: Martin M1 line = sync 4.862 | 0.572 | G | 0.572 | B | 0.572 | R | 0.572, scans 146.432 ms\n");
    {
        // R 255, G 0, B 128 everywhere.
        const std::vector<uint32_t> img(320 * 256, 0xff0080);
        const std::vector<float> a = encodeAll(sstv::Martin1, img, fs);
        const double line = 0.910 + 10 * 0.446446; // start of line 10
        const double sync = 0.004862, sep = 0.000572, scan = 0.146432;
        const double g0 = line + sync + sep, b0 = g0 + scan + sep, r0 = b0 + scan + sep;
        const double fSync = measureHz(a, fs, line + 0.0008, line + sync - 0.0008);
        const double fG = measureHz(a, fs, g0 + 0.01, g0 + scan - 0.01);
        const double fB = measureHz(a, fs, b0 + 0.01, b0 + scan - 0.01);
        const double fR = measureHz(a, fs, r0 + 0.01, r0 + scan - 0.01);
        std::printf("  sync %.1f  G %.1f (%.1f)  B %.1f (%.1f)  R %.1f (%.1f)\n",
                    fSync,
                    fG,
                    pixHz(0),
                    fB,
                    pixHz(128),
                    fR,
                    pixHz(255));
        CHECK(std::fabs(fSync - 1200.0) < 25.0);
        CHECK(std::fabs(fG - pixHz(0)) < 3.0);
        CHECK(std::fabs(fB - pixHz(128)) < 3.0);
        CHECK(std::fabs(fR - pixHz(255)) < 3.0);
    }

    std::printf("Reference: Scottie S1 line = 1.5 | G | 1.5 | B | sync 9 | 1.5 | R, scans 138.240 ms\n");
    {
        const std::vector<uint32_t> img(320 * 256, 0x40c0ff); // R 64, G 192, B 255
        const std::vector<float> a = encodeAll(sstv::Scottie1, img, fs);
        const double line = 0.910 + 0.009 + 5 * 0.428220; // header, start sync, 5 lines
        const double scan = 0.138240;
        const double g0 = line + 0.0015, b0 = g0 + scan + 0.0015, s0 = b0 + scan, r0 = s0 + 0.009 + 0.0015;
        const double fG = measureHz(a, fs, g0 + 0.01, g0 + scan - 0.01);
        const double fB = measureHz(a, fs, b0 + 0.01, b0 + scan - 0.01);
        const double fS = measureHz(a, fs, s0 + 0.0015, s0 + 0.0075);
        const double fR = measureHz(a, fs, r0 + 0.01, r0 + scan - 0.01);
        std::printf("  G %.1f (%.1f)  B %.1f (%.1f)  sync %.1f  R %.1f (%.1f)\n",
                    fG,
                    pixHz(192),
                    fB,
                    pixHz(255),
                    fS,
                    fR,
                    pixHz(64));
        CHECK(std::fabs(fG - pixHz(192)) < 3.0);
        CHECK(std::fabs(fB - pixHz(255)) < 3.0);
        CHECK(std::fabs(fS - 1200.0) < 15.0);
        CHECK(std::fabs(fR - pixHz(64)) < 3.0);
    }

    std::printf("Reference: Robot 36, white and black lines (Y 2300 / 1500 Hz, neutral colour 1900 Hz)\n");
    {
        // Lines 0-119 white, 120-239 black.
        std::vector<uint32_t> img(320 * 240, 0xffffff);
        std::fill(img.begin() + 320 * 120, img.end(), 0x000000);
        const std::vector<float> a = encodeAll(sstv::Robot36, img, fs);
        for (int y : {10, 11, 200, 201})
        {
            const double line = 0.910 + y * 0.150;
            const double y0 = line + 0.009 + 0.003, sep = y0 + 0.088, c0 = sep + 0.0045 + 0.0015;
            const double fY = measureHz(a, fs, y0 + 0.008, y0 + 0.080);
            const double fSep = measureHz(a, fs, sep + 0.0005, sep + 0.004);
            const double fC = measureHz(a, fs, c0 + 0.005, c0 + 0.039);
            const double wantY = y < 120 ? 2300.0 : 1500.0, wantSep = (y & 1) ? 2300.0 : 1500.0;
            std::printf("  line %d: Y %.1f (%.0f)  separator %.1f (%.0f)  chroma %.1f (1900)\n",
                        y,
                        fY,
                        wantY,
                        fSep,
                        wantSep,
                        fC);
            CHECK(std::fabs(fY - wantY) < 3.0);
            CHECK(std::fabs(fSep - wantSep) < 30.0);
            CHECK(std::fabs(fC - 1900.0) < 4.0);
        }
    }

    std::printf("Reference: PD90 grey 128 (Y, R-Y, B-Y, Y: 1902 / 1900 / 1900 / 1902 Hz), sync 20 ms\n");
    {
        const std::vector<uint32_t> img(320 * 256, 0x808080);
        const std::vector<float> a = encodeAll(sstv::PD90, img, fs);
        const double line = 0.910 + 3 * 0.70304;
        const double scan = 0.17024, y0 = line + 0.020 + 0.00208;
        const double fSync = measureHz(a, fs, line + 0.002, line + 0.018);
        CHECK(std::fabs(fSync - 1200.0) < 5.0);
        for (int k = 0; k < 4; ++k)
        {
            const double f = measureHz(a, fs, y0 + k * scan + 0.01, y0 + (k + 1) * scan - 0.01);
            const double want = (k == 0 || k == 3) ? pixHz(128) : 1900.0;
            std::printf("  part %d: %.1f (%.1f)\n", k, f, want);
            CHECK(std::fabs(f - want) < 4.0);
        }
    }
}

// Joined late: no VIS, the sync detector must find the mode and start.
static void noVisTests()
{
    const double fs = 48000.0;
    std::printf("No VIS: joined mid-picture, the mode is found from the sync pulses\n");
    for (int mode : {int(sstv::Martin1), int(sstv::Scottie1), int(sstv::Robot36), int(sstv::PD90), int(sstv::Martin2)})
    {
        const sstv::ModeInfo &m = sstv::info(mode);
        const std::vector<uint32_t> img = testImage(m.width, m.height);
        std::vector<float> a = encodeAll(mode, img, fs * (1.0 + 120e-6));
        const size_t skip = size_t((0.910 + 0.3 * sstv::imageSeconds(mode)) * fs); // 30 % in
        std::vector<float> audio(size_t(0.5 * fs), 0.0f);
        audio.insert(audio.end(), a.begin() + std::ptrdiff_t(skip), a.end());
        audio.insert(audio.end(), size_t(2.0 * fs), 0.0f);
        std::mt19937 rng(77 + mode);
        std::normal_distribution<float> nd(0.0f, 0.03f);
        for (float &v : audio)
            v = 0.5f * v + nd(rng);
        SstvDecoder dec;
        dec.setSampleRate(fs);
        int detected = -1;
        bool noVis = false;
        double startedAt = -1;
        for (size_t pos = 0; pos < audio.size(); pos += 1000)
        {
            const SstvDecoder::Events ev = dec.process(audio.data() + pos, std::min<size_t>(1000, audio.size() - pos));
            if (ev.started && detected < 0)
            {
                detected = dec.mode();
                noVis = ev.noVis;
                startedAt = double(pos) / fs;
            }
        }
        // Rows decoded should match the source rows from about 30 % down.
        double err = 999;
        if (detected == mode)
        {
            const int lines = dec.linesDone();
            const int first = int(0.3 * m.height) - 2;
            double best = 1e9;
            for (int off = first - 6; off <= first + 6; ++off)
            {
                double e = 0;
                long n = 0;
                for (int y = 2; y < std::min(lines, m.height - off) - 2; y += 3)
                    for (int x = 8; x < m.width - 8; x += 2)
                    {
                        const uint32_t p = img[size_t(y + off) * size_t(m.width) + size_t(x)];
                        const uint32_t q = dec.image()[size_t(y) * size_t(m.width) + size_t(x)] & 0xffffff;
                        for (int sh = 0; sh < 24; sh += 8)
                            e += std::abs(int((p >> sh) & 0xff) - int((q >> sh) & 0xff));
                        n += 3;
                    }
                if (n > 0)
                    best = std::min(best, e / double(n));
            }
            err = best;
        }
        std::printf(
            " %-10s detected %d noVis %d after %.1f s, err %.1f\n", m.name, detected, int(noVis), startedAt, err);
        CHECK(detected == mode);
        CHECK(noVis);
        CHECK(startedAt > 0 && startedAt < 0.5 + 9 * m.lineMs / 1000.0);
        CHECK(err < 20.0);
    }

    std::printf("No VIS: other digital modes in the passband start nothing\n");
    {
        // 8-tone MFSK at 62.5 baud (like Olivia 8/500 at 1500 Hz: two tones below 1350 Hz)
        // and PSK63 at 1000 Hz sending idle reversals.
        std::mt19937 rng(3);
        std::uniform_int_distribution<int> tone(0, 7);
        std::vector<float> mfsk, psk;
        double ph = 0.0;
        for (int sym = 0; sym < 62 * 30; ++sym)
        {
            const double f = 1500.0 - 250.0 + 31.25 * (tone(rng) + 0.5);
            for (int i = 0; i < 768; ++i)
            {
                ph += 2 * M_PI * f / fs;
                mfsk.push_back(float(0.5 * std::sin(ph)));
            }
        }
        double sign = 1.0;
        for (int sym = 0; sym < 63 * 30; ++sym)
        {
            sign = -sign; // a reversal every symbol, cosine-shaped
            for (int i = 0; i < 762; ++i)
            {
                const double t = double(sym * 762 + i) / fs;
                const double env = std::cos(M_PI * i / 762.0);
                psk.push_back(float(0.5 * sign * env * std::sin(2 * M_PI * 1000.0 * t)));
            }
        }
        for (const std::vector<float> *sig : {&mfsk, &psk})
        {
            SstvDecoder dec;
            dec.setSampleRate(fs);
            bool started = false;
            for (size_t pos = 0; pos + 1000 <= sig->size(); pos += 1000)
                started |= dec.process(sig->data() + pos, 1000).started;
            std::printf("  %s: %s\n", sig == &mfsk ? "MFSK" : "PSK63", started ? "STARTED" : "nothing");
            CHECK(!started);
        }
    }

    std::printf("No VIS: 20 s of band noise starts nothing\n");
    {
        std::vector<float> audio(size_t(20 * fs));
        std::mt19937 rng(5);
        std::normal_distribution<float> nd(0.0f, 0.3f);
        for (float &v : audio)
            v = nd(rng);
        SstvDecoder dec;
        dec.setSampleRate(fs);
        bool started = false;
        for (size_t pos = 0; pos < audio.size(); pos += 1000)
            started |= dec.process(audio.data() + pos, 1000).started;
        CHECK(!started);
    }
}

// Mistuned station: everything 70 Hz high (or low).
static void offsetTests()
{
    const double fs = 48000.0;
    std::printf("Offset: Robot 36 sent 70 Hz high, auto offset on\n");
    for (double off : {70.0, -90.0})
    {
        const sstv::ModeInfo &m = sstv::info(sstv::Robot36);
        const std::vector<uint32_t> img = testImage(m.width, m.height);
        std::vector<float> audio(size_t(0.4 * fs), 0.0f);
        const std::vector<float> a = encodeAll(sstv::Robot36, img, fs, off);
        audio.insert(audio.end(), a.begin(), a.end());
        audio.insert(audio.end(), size_t(1.5 * fs), 0.0f);
        for (float &v : audio)
            v *= 0.5f;
        for (int pass = 0; pass < 2; ++pass)
        {
            SstvDecoder dec;
            dec.setSampleRate(fs);
            if (pass == 1)
            {
                dec.setAutoOffset(false);
                dec.setOffsetHz(off);
            }
            int detected = -1;
            for (size_t pos = 0; pos < audio.size(); pos += 1000)
            {
                const SstvDecoder::Events ev
                    = dec.process(audio.data() + pos, std::min<size_t>(1000, audio.size() - pos));
                if (ev.started)
                    detected = dec.mode();
            }
            std::vector<uint32_t> got = dec.image();
            for (uint32_t &v : got)
                v &= 0xffffff;
            const double err = detected == sstv::Robot36 ? meanError(img, got, m.width, m.height) : 999.0;
            std::printf("  %+.0f Hz, %s: detected %d, offset %.1f Hz, err %.1f\n",
                        off,
                        pass ? "manual" : "auto",
                        detected,
                        dec.offsetHz(),
                        err);
            CHECK(detected == sstv::Robot36);
            CHECK(std::fabs(dec.offsetHz() - off) < 6.0);
            CHECK(err < 8.0);
        }
    }
    std::printf("Offset: without correction a 70 Hz error is not decoded as well\n");
    {
        SstvDecoder dec;
        dec.setSampleRate(fs);
        dec.setAutoOffset(false);
        const sstv::ModeInfo &m = sstv::info(sstv::Martin1);
        const std::vector<uint32_t> img = testImage(m.width, m.height);
        std::vector<float> audio(size_t(0.4 * fs), 0.0f);
        const std::vector<float> a = encodeAll(sstv::Martin1, img, fs, 70.0);
        audio.insert(audio.end(), a.begin(), a.end());
        int detected = -1;
        for (size_t pos = 0; pos < audio.size(); pos += 1000)
            if (dec.process(audio.data() + pos, std::min<size_t>(1000, audio.size() - pos)).started)
                detected = dec.mode();
        // The VIS still reads (70 Hz is inside its tolerance) but the colours are off.
        std::vector<uint32_t> got = dec.image();
        for (uint32_t &v : got)
            v &= 0xffffff;
        const double err = detected == sstv::Martin1 ? meanError(img, got, m.width, m.height) : 999.0;
        std::printf("  err %.1f\n", err);
        CHECK(err > 12.0);
    }
}

int main()
{
    referenceTests();
    noVisTests();
    offsetTests();

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
