// Qt-free tests of the multi-signal PSK list (PskMultiDecoder): several
// stations in one passband, each copied on its own row; nothing on noise.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_pskmulti.cpp engine/pskmultidecoder.cpp engine/pskcore.cpp
//        engine/spectrum.cpp -o test_pskmulti

#include "pskmultidecoder.h"

#include <algorithm>
#include <chrono>
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

struct Station
{
    std::string text;
    double hz;
    double amp; // peak amplitude
    double startSec;
};

// One PSK signal, starting at startSec, into a buffer of total seconds.
static void addStation(std::vector<float> &x, const Station &s, int speed, double fs)
{
    PskModulator m;
    m.setBaud(psk::baudFor(speed));
    m.setCarrier(s.hz);
    m.preroll(1.0);
    for (char c : s.text)
        m.queueChar(c);
    m.tail(0.3);
    std::vector<float> block(1024);
    size_t at = size_t(s.startSec * fs);
    int guard = 0;
    while (!m.idle() && guard++ < 100000 && at < x.size())
    {
        m.render(block.data(), block.size(), fs);
        for (size_t i = 0; i < block.size() && at + i < x.size(); ++i)
            x[at + i] += float(s.amp * block[i]);
        at += block.size();
    }
}

// PSKMULTI_SEED=n shifts every noise seed (to check the cases are not one lucky draw).
static unsigned g_seedShift = 0;

static void addNoise(std::vector<float> &x, double sigma, unsigned seed)
{
    std::mt19937 rng(seed + 1000u * g_seedShift);
    std::normal_distribution<double> nd(0.0, sigma);
    for (float &v : x)
        v = float(v + nd(rng));
}

// Noise sigma for a given S/N in the 31.25 Hz symbol bandwidth (per sample
// noise power spread over fs/2): S/N = (A^2/2) / (sigma^2 * B / (fs/2)).
static double sigmaFor(double amp, double snrDb, double baud, double fs)
{
    const double ps = amp * amp / 2.0;
    return std::sqrt(ps / std::pow(10.0, snrDb / 10.0) * (fs / 2.0) / baud);
}

static std::vector<PskMultiDecoder::ChannelInfo>
run(PskMultiDecoder &d, const std::vector<float> &x, double fs, double *cpuShare = nullptr)
{
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < x.size(); i += 2048)
        d.feed(x.data() + i, std::min<size_t>(2048, x.size() - i), fs);
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (cpuShare)
        *cpuShare = el / (double(x.size()) / fs);
    return d.channels();
}

static void printRows(const std::vector<PskMultiDecoder::ChannelInfo> &rows)
{
    for (const auto &r : rows)
        std::printf("    %7.1f Hz  %4.1f dB  [%s]\n", r.hz, r.snrDb, r.text.c_str());
}

// How much of truth a decoded text holds: 1 - (edit distance of the best-matching
// stretch of text) / truth length (junk before or after it costs nothing).
static double similarity(const std::string &text, const std::string &truth)
{
    const size_t m = truth.size(), n = text.size();
    if (m == 0)
        return 1.0;
    std::vector<size_t> prev(n + 1, 0), cur(n + 1, 0); // free start anywhere in text
    for (size_t i = 1; i <= m; ++i)
    {
        cur[0] = i;
        for (size_t j = 1; j <= n; ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (truth[i - 1] == text[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    const size_t best = *std::min_element(prev.begin(), prev.end()); // free end
    return 1.0 - double(best) / double(m);
}

// The row near hz copies the station about as well as the main decoder tuned there by
// hand does on the same audio (and at least minSim of it).
static bool copiesLike(const std::vector<PskMultiDecoder::ChannelInfo> &rows,
                       const std::vector<float> &x,
                       double fs,
                       int speed,
                       double hz,
                       const std::string &truth,
                       double minSim = 0.85)
{
    const PskMultiDecoder::ChannelInfo *row = nullptr;
    for (const auto &r : rows)
        if (std::fabs(r.hz - hz) < 10.0)
            row = &r;
    PskDemodulator d;
    d.setSampleRate(fs);
    d.setBaud(psk::baudFor(speed));
    d.setCarrier(hz);
    d.setAfc(true);
    d.setSquelch(0.3);
    std::string main;
    for (size_t i = 0; i < x.size(); i += 2048)
        main += d.process(x.data() + i, std::min<size_t>(2048, x.size() - i));
    const double rowSim = row ? similarity(row->text, truth) : 0.0;
    const double mainSim = similarity(main, truth);
    std::printf("    %7.1f Hz: row copies %3.0f %%, main decoder tuned there %3.0f %%\n", hz, 100.0 * rowSim,
                100.0 * mainSim);
    return row && rowSim >= minSim && rowSim >= mainSim - 0.10;
}

int main()
{
    if (const char *e = std::getenv("PSKMULTI_SEED"))
        g_seedShift = unsigned(std::atoi(e));
    const std::string t1 = "CQ CQ CQ DE W1AW W1AW PSE K";
    const std::string t2 = "KC9OXP DE N0CALL UR RSQ 599 IN EN71 BTU";
    const std::string t3 = "TEST DE DL1ABC DL1ABC QRL? 73 SK";

    std::printf("=== three BPSK31 stations at 48 kHz, staggered, 18-24 dB ===\n");
    {
        const double fs = 48000.0;
        std::vector<float> x(size_t(36.0 * fs), 0.0f);
        addStation(x, {t1, 712.0, 0.10, 0.5}, psk::Bpsk31, fs);
        addStation(x, {t2, 1103.3, 0.10, 6.0}, psk::Bpsk31, fs);
        addStation(x, {t3, 1857.0, 0.20, 12.0}, psk::Bpsk31, fs);
        addNoise(x, sigmaFor(0.10, 18.0, 31.25, fs), 11);
        PskMultiDecoder d;
        double cpu = 0.0;
        const auto rows = run(d, x, fs, &cpu);
        printRows(rows);
        std::printf("    CPU %.2f %% of one core\n", 100.0 * cpu);
        CHECK(rows.size() == 3);
        CHECK(copiesLike(rows, x, fs, psk::Bpsk31, 712.0, t1));
        CHECK(copiesLike(rows, x, fs, psk::Bpsk31, 1103.3, t2));
        CHECK(copiesLike(rows, x, fs, psk::Bpsk31, 1857.0, t3));
        CHECK(cpu < 0.10);
    }

    std::printf("=== two BPSK31 stations 60 Hz apart, 12 kHz audio ===\n");
    {
        const double fs = 12000.0;
        std::vector<float> x(size_t(30.0 * fs), 0.0f);
        addStation(x, {t1, 1000.0, 0.10, 0.5}, psk::Bpsk31, fs);
        addStation(x, {t2, 1060.0, 0.10, 1.5}, psk::Bpsk31, fs);
        addNoise(x, sigmaFor(0.10, 15.0, 31.25, fs), 5);
        PskMultiDecoder d;
        const auto rows = run(d, x, fs);
        printRows(rows);
        CHECK(rows.size() == 2);
        CHECK(copiesLike(rows, x, fs, psk::Bpsk31, 1000.0, t1));
        CHECK(copiesLike(rows, x, fs, psk::Bpsk31, 1060.0, t2));
    }

    std::printf("=== BPSK63 list (speed set to 62.5 baud) ===\n");
    {
        const double fs = 48000.0;
        std::vector<float> x(size_t(25.0 * fs), 0.0f);
        addStation(x, {t1 + " " + t1, 900.0, 0.10, 0.5}, psk::Bpsk63, fs);
        addStation(x, {t3 + " " + t3, 1500.0, 0.10, 2.0}, psk::Bpsk63, fs);
        addNoise(x, sigmaFor(0.10, 15.0, 62.5, fs), 7);
        PskMultiDecoder d;
        d.setBaud(62.5);
        const auto rows = run(d, x, fs);
        printRows(rows);
        CHECK(rows.size() == 2);
        CHECK(copiesLike(rows, x, fs, psk::Bpsk63, 900.0, t1 + " " + t1));
        CHECK(copiesLike(rows, x, fs, psk::Bpsk63, 1500.0, t3 + " " + t3));
    }

    std::printf("=== weak station (10 dB) beside a strong one (34 dB) ===\n");
    {
        const double fs = 48000.0;
        std::vector<float> x(size_t(30.0 * fs), 0.0f);
        const double sigma = sigmaFor(0.02, 10.0, 31.25, fs);
        addStation(x, {t1, 800.0, 0.02 * std::pow(10.0, 24.0 / 20.0), 0.5}, psk::Bpsk31, fs);
        addStation(x, {t2, 1400.0, 0.02, 1.0}, psk::Bpsk31, fs);
        addNoise(x, sigma, 9);
        PskMultiDecoder d;
        const auto rows = run(d, x, fs);
        printRows(rows);
        CHECK(copiesLike(rows, x, fs, psk::Bpsk31, 800.0, t1));
        // weak: about what the main decoder copies there
        CHECK(copiesLike(rows, x, fs, psk::Bpsk31, 1400.0, t2, 0.60));
        CHECK(rows.size() == 2);
    }

    std::printf("=== noise only, 120 s: no rows ===\n");
    {
        const double fs = 48000.0;
        std::vector<float> x(size_t(120.0 * fs), 0.0f);
        addNoise(x, 0.05, 3);
        PskMultiDecoder d;
        const auto rows = run(d, x, fs);
        printRows(rows);
        size_t chars = 0;
        for (const auto &r : rows)
            chars += r.text.size();
        std::printf("    junk characters: %zu\n", chars);
        CHECK(chars <= 3);
    }

    std::printf("=== steady carriers (no PSK): no rows ===\n");
    {
        const double fs = 48000.0;
        std::vector<float> x(size_t(30.0 * fs), 0.0f);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = float(0.1 * std::sin(2.0 * M_PI * 1000.0 * i / fs) + 0.1 * std::sin(2.0 * M_PI * 1750.0 * i / fs));
        addNoise(x, 0.02, 4);
        PskMultiDecoder d;
        const auto rows = run(d, x, fs);
        printRows(rows);
        CHECK(rows.empty());
    }

    std::printf("=== a station that stops is dropped after 30 s; speed change clears ===\n");
    {
        const double fs = 12000.0;
        std::vector<float> x(size_t(50.0 * fs), 0.0f);
        addStation(x, {t1, 1200.0, 0.10, 0.5}, psk::Bpsk31, fs);
        addNoise(x, sigmaFor(0.10, 15.0, 31.25, fs), 6);
        PskMultiDecoder d;
        std::vector<float> first(x.begin(), x.begin() + long(15.0 * fs));
        std::vector<float> rest(x.begin() + long(15.0 * fs), x.end());
        CHECK(run(d, first, fs).size() == 1);
        CHECK(run(d, rest, fs).empty());
        run(d, first, fs);
        CHECK(d.channels().size() == 1);
        d.setBaud(62.5);
        CHECK(d.channels().empty());
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
