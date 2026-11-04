// Qt-free tests of the PSK modem: Varicode table, modulator -> demodulator
// loopback at several speeds, sample rates, frequency offsets and noise.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_psk.cpp engine/pskcore.cpp -o test_psk

#include "pskcore.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
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

static std::vector<float> modulate(const std::string &text, int speed, double carrier, double fs, size_t *padOut)
{
    PskModulator m;
    m.setBaud(psk::baudFor(speed));
    m.setCarrier(carrier);
    m.preroll(1.0);
    for (char c : text)
        m.queueChar(c);
    m.tail(0.3);
    std::vector<float> out;
    // leading silence of a random-ish length so symbol timing is arbitrary
    const size_t pad = size_t(0.37 * fs) + 113;
    out.assign(pad, 0.0f);
    std::vector<float> block(1024);
    int guard = 0;
    while (!m.idle() && guard++ < 100000)
    {
        m.render(block.data(), block.size(), fs);
        out.insert(out.end(), block.begin(), block.end());
    }
    out.insert(out.end(), size_t(0.5 * fs), 0.0f);
    if (padOut)
        *padOut = pad;
    return out;
}

static std::string demodulate(const std::vector<float> &x,
                              int speed,
                              double tuneHz,
                              double fs,
                              double squelch,
                              double *finalHz = nullptr,
                              double *quality = nullptr)
{
    PskDemodulator d;
    d.setSampleRate(fs);
    d.setBaud(psk::baudFor(speed));
    d.setCarrier(tuneHz);
    d.setAfc(true);
    d.setSquelch(squelch);
    std::string out;
    double q = 0.0;
    for (size_t i = 0; i < x.size(); i += 777)
    {
        const size_t n = std::min<size_t>(777, x.size() - i);
        const std::string part = d.process(x.data() + i, n);
        out += part;
        q = std::max(q, d.stats().quality);
        if (!part.empty() && finalHz)
            *finalHz = d.stats().carrierHz; // AFC while the signal is there
    }
    if (quality)
        *quality = q;
    return out;
}

static void addNoise(std::vector<float> &x, double amp, double sigma, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, sigma);
    for (float &v : x)
        v = float(v * amp + nd(rng));
}

int main()
{
    std::printf("=== Varicode table ===\n");
    {
        std::set<std::string> seen;
        bool ok = true;
        for (int c = 0; c < 128; ++c)
        {
            const char *v = psk::varicode((unsigned char)c);
            const size_t len = std::strlen(v);
            if (len == 0 || len > 10 || v[0] != '1' || v[len - 1] != '1' || std::strstr(v, "00"))
            {
                std::printf("  bad code for %d: %s\n", c, v);
                ok = false;
            }
            if (!seen.insert(v).second)
            {
                std::printf("  duplicate code for %d: %s\n", c, v);
                ok = false;
            }
            uint32_t bits = 0;
            for (const char *p = v; *p; ++p)
                bits = (bits << 1) | uint32_t(*p == '1');
            if (psk::varicodeDecode(bits) != c)
                ok = false;
        }
        CHECK(ok);
        CHECK(std::strcmp(psk::varicode(' '), "1") == 0);
        CHECK(std::strcmp(psk::varicode('e'), "11") == 0);
        CHECK(std::strcmp(psk::varicode('t'), "101") == 0);
        CHECK(std::strcmp(psk::varicode('o'), "111") == 0);
        CHECK(std::strcmp(psk::varicode('a'), "1011") == 0);
        CHECK(std::strcmp(psk::varicode('\n'), "11101") == 0);
        CHECK(std::strcmp(psk::varicode('\r'), "11111") == 0);
        CHECK(std::strcmp(psk::varicode('E'), "1110111") == 0);
        CHECK(psk::varicode(200) == nullptr);

        PskVaricodeDecoder dec;
        std::string got;
        const std::string text = "Hello 73 de KC9oXP!";
        for (char c : text)
        {
            for (const char *p = psk::varicode((unsigned char)c); *p; ++p)
                dec.push(*p == '1');
            dec.push(0);
            const int r = dec.push(0);
            if (r >= 0)
                got.push_back(char(r));
        }
        CHECK(got == text);
    }

    const std::string text = "CQ CQ de KC9oXP KC9oXP pse k\nThe quick brown fox 0123456789 /?.,\n";
    struct Case
    {
        int speed;
        double fs;
        double carrier;
        double tune; // receiver tuned here (AFC pulls in)
        double snrSigma;
    };
    const Case cases[] = {
        {psk::Bpsk31, 48000.0, 1000.0, 1000.0, 0.0},
        {psk::Bpsk31, 48000.0, 1500.0, 1504.0, 0.05},
        {psk::Bpsk31, 8000.0, 1200.0, 1197.0, 0.05},
        {psk::Bpsk31, 44100.0, 1000.0, 1002.0, 0.05},
        {psk::Bpsk63, 48000.0, 1000.0, 1006.0, 0.05},
        {psk::Bpsk125, 48000.0, 1800.0, 1790.0, 0.05},
        {psk::Bpsk125, 12000.0, 900.0, 900.0, 0.05},
    };
    for (const Case &c : cases)
    {
        std::printf("\n=== %s, %.0f Hz sample rate, carrier %.0f Hz, tuned %.0f Hz, noise %.2f ===\n",
                    psk::speedName(c.speed),
                    c.fs,
                    c.carrier,
                    c.tune,
                    c.snrSigma);
        std::vector<float> x = modulate(text, c.speed, c.carrier, c.fs, nullptr);
        // Signal at 0.3 of full scale; 0.05 noise in the full 0..fs/2 band is
        // roughly +10 dB SNR in the 31 Hz signal bandwidth at 48 kHz.
        addNoise(x, 0.3, c.snrSigma, 11);
        double hz = 0.0, q = 0.0;
        const std::string got = demodulate(x, c.speed, c.tune, c.fs, 0.3, &hz, &q);
        std::printf("  decoded: %s\n", got.c_str());
        std::printf("  AFC end %.2f Hz, peak quality %.2f\n", hz, q);
        CHECK(got.find("CQ CQ de KC9oXP KC9oXP pse k") != std::string::npos);
        CHECK(got.find("The quick brown fox 0123456789 /?.,") != std::string::npos);
        CHECK(std::fabs(hz - c.carrier) < 1.5);
    }

    std::printf("\n=== weak signal: BPSK31 at 48 kHz, noise 0.25 (about -4 dB in 31 Hz) ===\n");
    {
        std::vector<float> x = modulate(text, psk::Bpsk31, 1000.0, 48000.0, nullptr);
        addNoise(x, 0.3, 0.25, 5);
        const std::string got = demodulate(x, psk::Bpsk31, 1000.0, 48000.0, 0.0);
        std::printf("  decoded: %s\n", got.c_str());
        size_t match = 0;
        const std::string ref = "The quick brown fox 0123456789";
        for (size_t i = 0; i + ref.size() <= got.size(); ++i)
        {
            size_t m = 0;
            for (size_t j = 0; j < ref.size(); ++j)
                m += got[i + j] == ref[j];
            match = std::max(match, m);
        }
        std::printf("  best match %zu/%zu characters\n", match, ref.size());
        CHECK(match >= ref.size() * 8 / 10);
    }

    std::printf("\n=== squelch closed on noise ===\n");
    {
        std::vector<float> x(size_t(48000 * 10), 0.0f);
        addNoise(x, 0.0, 0.2, 9);
        double q = 0.0;
        const std::string got = demodulate(x, psk::Bpsk31, 1000.0, 48000.0, 0.3, nullptr, &q);
        std::printf("  %zu characters from 10 s of noise, peak quality %.2f\n", got.size(), q);
        CHECK(got.size() <= 2);
    }

    std::printf("\n=== modulator: fade in/out, echo count ===\n");
    {
        PskModulator m;
        m.setBaud(31.25);
        m.setCarrier(1000.0);
        m.preroll(0.2);
        const size_t afterA = m.queueChar('a');
        const size_t afterNl = m.queueChar('\n');
        CHECK(afterA == 4 + 2);
        CHECK(afterNl == afterA + (5 + 2) + (5 + 2)); // CR + LF
        CHECK(m.queueChar(char(200)) == 0);
        m.tail(0.1);
        // Idle while keyed: after the text, idle reversals go out and idle() is true.
        {
            PskModulator k;
            k.setBaud(31.25);
            k.preroll(0.1);
            k.queueChar('e');
            std::vector<float> o(48000);
            k.render(o.data(), o.size(), 48000.0);
            CHECK(k.idle());
            CHECK(std::fabs(o.back()) + std::fabs(o[o.size() - 300]) > 0.0f); // still keyed
        }
        std::vector<float> out(48000 * 3);
        m.render(out.data(), out.size(), 48000.0);
        CHECK(m.idle());
        CHECK(m.unitsSent() == afterNl);
        CHECK(std::fabs(out[0]) < 1e-3); // starts from silence
        float peak = 0.0f;
        for (float v : out)
            peak = std::max(peak, std::fabs(v));
        CHECK(peak <= 1.0001f && peak > 0.99f);
        CHECK(std::fabs(out.back()) < 1e-6); // ended silent
    }

    std::printf("\n=== findCenter ===\n");
    {
        std::vector<float> x
            = modulate("the quick brown fox jumps over the lazy dog", psk::Bpsk31, 1234.0, 8000.0, nullptr);
        addNoise(x, 0.3, 0.05, 3);
        const double c = psk::findCenter(x.data(), x.size(), 8000.0, 1240.0, 31.25);
        std::printf("  clicked 1240, found %.2f (true 1234)\n", c);
        CHECK(std::fabs(c - 1234.0) < 2.0);
        std::vector<float> n(8000 * 3, 0.0f);
        addNoise(n, 0.0, 0.1, 4);
        const double c2 = psk::findCenter(n.data(), n.size(), 8000.0, 1500.0, 31.25);
        std::printf("  noise only: clicked 1500, returned %.2f\n", c2);
        CHECK(std::fabs(c2 - 1500.0) < 30.0);
    }

    std::printf(g_fail ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
