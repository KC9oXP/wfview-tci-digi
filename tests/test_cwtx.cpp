// Qt-free tests of CW transmit (audio CW): the keyer's timing and shaping, and
// CwTxSource -> the app's own CW decoder (both engines) as a loopback.
// Build: g++ -std=c++17 -O2 -Isrc -Iengine -Iengine/ggmorse/include tests/test_cwtx.cpp engine/cwtxcore.cpp
//        engine/modemtxsource.cpp engine/pskcore.cpp engine/hellcore.cpp engine/ggmorse/src/ggmorse.cpp
//        engine/ggmorse/src/resampler.cpp -o test_cwtx

#include "cwdecodercore.h"
#include "cwtxcore.h"
#include "modemtxsource.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
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

// Render everything queued in src, at rate, in blocks, as the TCI client would (stereo).
static std::vector<float> renderAll(CwTxSource &src, double rate)
{
    std::vector<float> mono;
    std::vector<float> stereo(2 * 1024);
    for (int guard = 0; guard < 100000; ++guard)
    {
        src.render(stereo.data(), 1024, 2, float(rate));
        for (size_t i = 0; i < 1024; ++i)
            mono.push_back(stereo[2 * i]);
        if (src.idle())
            break;
    }
    mono.insert(mono.end(), size_t(rate * 1.5), 0.0f);
    return mono;
}

static std::string decode(const std::vector<float> &x, double rate, CwDecoderCore::Engine e, float pitch)
{
    CwDecoderCore core;
    core.setEngine(e);
    core.setTrackRange(200.0f, 1200.0f, -1.0f);
    (void)pitch;
    std::string got, out;
    for (size_t pos = 0; pos + 1024 <= x.size(); pos += 1024)
    {
        out.clear();
        core.feed(x.data() + pos, 1024, float(rate), out);
        got += out;
    }
    return got;
}

int main()
{
    std::printf("=== Morse table ===\n");
    CHECK(std::strcmp(cw::morse('a'), ".-") == 0);
    CHECK(std::strcmp(cw::morse('0'), "-----") == 0);
    CHECK(std::strcmp(cw::morse('/'), "-..-.") == 0);
    CHECK(cw::morse('~') == nullptr);
    CHECK(std::strcmp(cw::prosign("sk"), "...-.-") == 0);
    CHECK(cw::prosign("ZZ") == nullptr);

    std::printf("\n=== timing (PARIS = 50 units) ===\n");
    {
        CwModulator m;
        m.setWpm(20.0);
        m.setPitch(700.0);
        m.preroll(0.0);
        size_t after = 0;
        for (char c : std::string("PARIS "))
            after = m.queueChar(c);
        CHECK(after == 50);
        std::vector<float> out(48000 * 4);
        m.render(out.data(), out.size(), 48000.0);
        CHECK(m.idle());
        CHECK(m.unitsSent() == 50);
        // first mark starts at 0; last mark (S's third dot) ends at unit 43 (+ 3 + 4 gap)
        size_t first = 0, last = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::fabs(out[i]) > 1e-3)
            {
                if (!first)
                    first = i;
                last = i;
            }
        const double dot = 1.2 / 20.0 * 48000.0;
        std::printf("  last keyed sample %zu, expected about %.0f\n", last, 43.0 * dot);
        CHECK(std::fabs(double(last) - 43.0 * dot) < 0.006 * 48000.0);
        float peak = 0.0f;
        for (float v : out)
            peak = std::max(peak, std::fabs(v));
        CHECK(peak > 0.99f && peak <= 1.0001f);
        CHECK(m.queueChar('~') == 0);
    }

    std::printf("\n=== key clicks: power outside +/-150 Hz ===\n");
    {
        CwModulator m;
        m.setWpm(30.0);
        m.setPitch(700.0);
        m.preroll(0.0);
        for (char c : std::string("TEST TEST 5NN"))
            m.queueChar(c);
        std::vector<float> x(8000 * 5);
        m.render(x.data(), x.size(), 8000.0);
        double in = 0.0, outBand = 0.0;
        for (double f = 200.0; f <= 1800.0; f += 4.0)
        {
            std::complex<double> acc(0.0, 0.0);
            for (size_t i = 0; i < x.size(); ++i)
                acc += double(x[i]) * std::polar(1.0, -2.0 * M_PI * f * double(i) / 8000.0);
            (std::fabs(f - 700.0) <= 150.0 ? in : outBand) += std::norm(acc);
        }
        const double db = 10.0 * std::log10(outBand / in);
        std::printf("  %.1f dB outside +/-150 Hz\n", db);
        CHECK(db < -30.0);
    }

    std::printf("\n=== CwTxSource: prosigns and echo ===\n");
    {
        CwTxSource s;
        s.setWpm(40.0);
        s.setCarrier(700.0);
        s.setLevel(0.5f);
        s.start(0.0);
        s.queueText("TU 73 <SK> ~");
        std::vector<float> mono = renderAll(s, 48000.0);
        const std::string sent = s.takeSent();
        std::printf("  echo: [%s]\n", sent.c_str());
        CHECK(sent == "TU 73 <SK> ");
        float peak = 0.0f;
        for (float v : mono)
            peak = std::max(peak, std::fabs(v));
        CHECK(std::fabs(peak - 0.5f) < 0.01f); // TX level applied
    }

    std::printf("\n=== loopback through the CW decoder ===\n");
    struct Case
    {
        double wpm, pitch, rate;
        CwDecoderCore::Engine engine;
    };
    const Case cases[] = {
        {20, 700, 48000, CwDecoderCore::Engine::Machine},
        {28, 600, 48000, CwDecoderCore::Engine::Machine},
        {15, 800, 44100, CwDecoderCore::Engine::Machine},
        {20, 700, 48000, CwDecoderCore::Engine::Hand},
    };
    for (const Case &c : cases)
    {
        CwTxSource s;
        s.setWpm(c.wpm);
        s.setCarrier(c.pitch);
        s.setLevel(0.3f);
        s.start(0.5);
        s.queueText("CQ CQ DE KC9OXP KC9OXP K ");
        std::vector<float> x = renderAll(s, c.rate);
        std::mt19937 rng(5);
        std::normal_distribution<float> nd(0.0f, 0.02f);
        for (float &v : x)
            v += nd(rng);
        const std::string got = decode(x, c.rate, c.engine, float(c.pitch));
        std::printf("  %s %.0f WPM %.0f Hz %.0f Hz rate -> [%s]\n",
                    c.engine == CwDecoderCore::Engine::Hand ? "Hand key" : "Machine",
                    c.wpm,
                    c.pitch,
                    c.rate,
                    got.c_str());
        CHECK(got.find("KC9OXP") != std::string::npos);
    }

    std::printf(g_fail ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
