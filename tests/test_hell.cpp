// Qt-free tests of the Feld Hell modem: font, keying timing and bandwidth,
// and modulator -> demodulator loopback "read back" by matching the received
// columns against the font.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_hell.cpp engine/hellcore.cpp -o test_hell

#include "hellcore.h"

#include <cmath>
#include <complex>
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

static const std::string kAlphabet = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./?-=";

static std::vector<float> modulate(const std::string &text, double fs, double carrier, size_t *samplesPerChar)
{
    HellModulator m;
    m.setCarrier(carrier);
    m.preroll(0.5);
    for (char c : text)
        m.queueChar(c);
    m.tail(0.3);
    std::vector<float> out(size_t(0.21 * fs), 0.0f); // arbitrary start offset
    std::vector<float> block(1000);
    int guard = 0;
    while (!m.idle() && guard++ < 100000)
    {
        m.render(block.data(), block.size(), fs);
        out.insert(out.end(), block.begin(), block.end());
    }
    out.insert(out.end(), size_t(0.3 * fs), 0.0f);
    if (samplesPerChar)
        *samplesPerChar = size_t(hell::kCharCols * hell::kRows / hell::kPixelRate * fs);
    return out;
}

// Reads the half-pixel stream back as text: tries every alignment and picks
// the one whose characters best match the font.
static std::string readBack(const std::vector<float> &px)
{
    const int perChar = hell::kCharCols * hell::kRows;
    std::vector<std::vector<uint8_t>> templ;
    for (char c : kAlphabet)
    {
        std::vector<uint8_t> cols;
        hell::charColumns(c, cols);
        templ.push_back(cols);
    }
    std::string best;
    double bestScore = 1e18;
    for (int off = 0; off < perChar; ++off)
    {
        std::string s;
        double score = 0.0;
        for (size_t start = size_t(off); start + size_t(perChar) <= px.size(); start += size_t(perChar))
        {
            int bi = 0;
            double bd = 1e18;
            for (size_t t = 0; t < templ.size(); ++t)
            {
                double d = 0.0;
                for (int i = 0; i < perChar; ++i)
                {
                    const double e = px[start + size_t(i)] - templ[t][size_t(i)];
                    d += e * e;
                }
                if (d < bd)
                {
                    bd = d;
                    bi = int(t);
                }
            }
            s.push_back(kAlphabet[size_t(bi)]);
            score += bd;
        }
        if (score < bestScore)
        {
            bestScore = score;
            best = s;
        }
    }
    // trim the blank preroll/tail
    const size_t a = best.find_first_not_of(' ');
    const size_t b = best.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : best.substr(a, b - a + 1);
}

static std::string loopback(const std::string &text, double fs, double carrier, double tune, double noise, double gain)
{
    std::vector<float> x = modulate(text, fs, carrier, nullptr);
    std::mt19937 rng(7);
    std::normal_distribution<double> nd(0.0, noise);
    for (float &v : x)
        v = float(v * gain + nd(rng));
    HellDemodulator d;
    d.setSampleRate(fs);
    d.setCarrier(tune);
    std::vector<float> cols;
    for (size_t i = 0; i < x.size(); i += 512)
        d.process(x.data() + i, std::min<size_t>(512, x.size() - i), cols);
    return readBack(cols);
}

int main()
{
    std::printf("=== font ===\n");
    {
        bool ok = true;
        for (char c : kAlphabet)
            if (!hell::glyph(c))
                ok = false;
        CHECK(ok);
        CHECK(hell::glyph('a') == hell::glyph('A'));
        CHECK(hell::glyph('~') == nullptr);
        // all glyphs distinct
        bool distinct = true;
        for (size_t i = 0; i < kAlphabet.size(); ++i)
            for (size_t j = i + 1; j < kAlphabet.size(); ++j)
            {
                std::vector<uint8_t> a, b;
                hell::charColumns(kAlphabet[i], a);
                hell::charColumns(kAlphabet[j], b);
                if (a == b)
                    distinct = false;
            }
        CHECK(distinct);
    }

    std::printf("\n=== modulator timing ===\n");
    {
        HellModulator m;
        m.setCarrier(1000.0);
        m.preroll(0.0); // one blank column
        const size_t afterA = m.queueChar('A');
        CHECK(afterA == 7);
        CHECK(m.queueChar('~') == 0);
        const size_t afterNl = m.queueChar('\n'); // sent as a space
        CHECK(afterNl == 14);
        std::vector<float> out(48000 * 2);
        m.render(out.data(), out.size(), 48000.0);
        CHECK(m.idle());
        CHECK(m.unitsSent() == 14);
        // 1 blank + 14 columns = 15 columns at 17.5 columns per second
        size_t last = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::fabs(out[i]) > 1e-4)
                last = i;
        const double expectEnd = (1.0 + 5.0) * hell::kRows / hell::kPixelRate * 48000.0; // 'A' glyph ends at col 6
        std::printf("  last keyed sample %zu, expected about %.0f\n", last, expectEnd);
        CHECK(std::fabs(double(last) - expectEnd) < 48000.0 * 2.0 / hell::kPixelRate);
    }

    std::printf("\n=== occupied bandwidth ===\n");
    {
        std::vector<float> x = modulate("RYRY HELL TEST 0123456789", 8000.0, 1500.0, nullptr);
        // Power in 1250..1750 Hz versus everything else, by DFT at 5 Hz steps.
        double in = 0.0, outBand = 0.0;
        for (double f = 300.0; f <= 2700.0; f += 5.0)
        {
            std::complex<double> acc(0.0, 0.0);
            for (size_t i = 0; i < x.size(); ++i)
                acc += double(x[i]) * std::polar(1.0, -2.0 * M_PI * f * double(i) / 8000.0);
            const double p = std::norm(acc);
            (std::fabs(f - 1500.0) <= 250.0 ? in : outBand) += p;
        }
        const double ratioDb = 10.0 * std::log10(outBand / in);
        std::printf("  power outside +/-250 Hz: %.1f dB relative to inside\n", ratioDb);
        CHECK(ratioDb < -20.0);
    }

    const std::string text = "CQ CQ DE KC9OXP KC9OXP PSE K 73";
    std::printf("\n=== loopback, clean, 48 kHz ===\n");
    {
        const std::string got = loopback(text, 48000.0, 1000.0, 1000.0, 0.0, 0.3);
        std::printf("  read back: \"%s\"\n", got.c_str());
        CHECK(got == text);
    }
    std::printf("\n=== loopback, noise, 8 kHz, tuned 20 Hz off ===\n");
    {
        const std::string got = loopback(text, 8000.0, 1500.0, 1520.0, 0.08, 0.3);
        std::printf("  read back: \"%s\"\n", got.c_str());
        CHECK(got == text);
    }
    std::printf("\n=== loopback, heavy noise, 44.1 kHz ===\n");
    {
        const std::string got = loopback(text, 44100.0, 1200.0, 1200.0, 0.4, 0.3);
        size_t same = 0;
        for (size_t i = 0; i < std::min(got.size(), text.size()); ++i)
            same += got[i] == text[i];
        std::printf("  read back: \"%s\" (%zu/%zu)\n", got.c_str(), same, text.size());
        CHECK(same >= text.size() * 8 / 10);
    }

    std::printf(g_fail ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
