// Qt-free tests of RSID receive (engine/rsidcore): fldigi's code words,
// bursts found at several frequencies, sample rates and noise levels, the
// right way up and mirrored, and no false IDs on noise or on other signals.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_rsid.cpp engine/rsidcore.cpp engine/spectrum.cpp engine/pskcore.cpp -o
// test_rsid

#include "pskcore.h"
#include "rsidcore.h"

#include <cmath>
#include <cstdio>
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

static std::vector<rsid::Hit> run(rsid::Detector &d, const std::vector<float> &x, size_t block)
{
    std::vector<rsid::Hit> all;
    for (size_t i = 0; i < x.size(); i += block)
    {
        const auto h = d.process(x.data() + i, std::min(block, x.size() - i));
        all.insert(all.end(), h.begin(), h.end());
    }
    return all;
}

static void addNoise(std::vector<float> &x, double sigma, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, sigma);
    for (float &v : x)
        v = float(v + nd(rng));
}

int main()
{
    std::printf("=== code words (fldigi's encoder) ===\n");
    {
        // Every ID in the table gives a different word, and words differ in
        // many places (Reed-Solomon (15,3): at least 13).
        std::set<std::vector<unsigned char>> words;
        int minDist = 99;
        std::vector<std::vector<unsigned char>> all;
        for (const rsid::Mode &m : rsid::modes())
        {
            std::vector<unsigned char> w(rsid::kSymbols);
            rsid::encode(m.code, w.data());
            for (unsigned char t : w)
                if (t > 15)
                    ++g_fail;
            words.insert(w);
            all.push_back(w);
        }
        for (size_t a = 0; a < all.size(); ++a)
            for (size_t b = a + 1; b < all.size(); ++b)
            {
                int d = 0;
                for (int s = 0; s < rsid::kSymbols; ++s)
                    d += all[a][size_t(s)] != all[b][size_t(s)];
                minDist = std::min(minDist, d);
            }
        std::printf("  %zu IDs, minimum distance %d\n", all.size(), minDist);
        CHECK(words.size() == rsid::modes().size());
        CHECK(minDist >= 13);
        CHECK(rsid::modeFor(69) && rsid::displayName(*rsid::modeFor(69)) == "Olivia 8/250");
        CHECK(rsid::modeFor(55) && rsid::displayName(*rsid::modeFor(55)) == "Contestia 4/250");
        CHECK(rsid::modeFor(2) && rsid::modeFor(2)->target == rsid::Target::Psk && rsid::modeFor(2)->pskSpeed == 1);
        CHECK(rsid::modeFor(39) && rsid::modeFor(39)->rttyShift == 170
              && std::fabs(rsid::modeFor(39)->rttyBaud - 45.45) < 1e-9);
        CHECK(rsid::modeFor(12) && rsid::modeFor(12)->target == rsid::Target::Mt63
              && rsid::modeFor(12)->bandwidth == 1000 && rsid::modeFor(12)->longInterleave); // MT63-1000L
        CHECK(rsid::displayName(*rsid::modeFor(13)) == "MT63-1000S");
        CHECK(rsid::modeFor(57) && rsid::modeFor(57)->target == rsid::Target::None); // MFSK16: not decoded here
        CHECK(rsid::modeFor(6) == nullptr); // escape to the second table: not listed
    }

    std::printf("\n=== bursts: modes, frequencies, sample rates, mirrored, noise ===\n");
    {
        struct Case
        {
            int code;
            double hz;
            double fs;
            bool mirrored;
            double sigma;
        };
        const Case cases[] = {
            {69, 1500.0, 48000.0, false, 0.05},
            {69, 1500.0, 48000.0, true, 0.05},
            {1, 1000.0, 48000.0, false, 0.3},
            {2, 2210.0, 12000.0, false, 0.3},
            {4, 640.0, 8000.0, true, 0.3},
            {39, 2125.0, 48000.0, false, 0.3},
            {41, 1700.0, 44100.0, true, 0.3},
            {55, 1897.0, 48000.0, false, 0.6}, // weak: about -6 dB in 3 kHz
            {204, 350.0, 48000.0, false, 0.3},
            {104, 1200.0, 48000.0, false, 0.3},
            {263, 1500.0, 48000.0, false, 0.3}, // EOT
            {12, 1500.0, 48000.0, false, 0.3}, // MT63-1000L
            {57, 1500.0, 48000.0, false, 0.3}, // MFSK16: found, not decoded here
        };
        unsigned seed = 1;
        for (const Case &c : cases)
        {
            rsid::Detector d;
            d.setSampleRate(c.fs);
            d.setRange(200.0, 3000.0);
            std::vector<float> x(size_t(1.7 * c.fs), 0.0f);
            const std::vector<float> b = rsid::makeBurst(c.code, c.hz, c.fs, c.mirrored);
            x.insert(x.end(), b.begin(), b.end());
            x.insert(x.end(), size_t(2.0 * c.fs), 0.0f);
            addNoise(x, c.sigma, seed++);
            const auto hits = run(d, x, 960);
            std::printf("  code %d at %.0f Hz, fs %.0f%s, noise %.2f: %zu hit(s)",
                        c.code,
                        c.hz,
                        c.fs,
                        c.mirrored ? " mirrored" : "",
                        c.sigma,
                        hits.size());
            if (!hits.empty())
                std::printf(" -> code %d at %.1f Hz%s, %d wrong",
                            hits[0].code,
                            hits[0].centreHz,
                            hits[0].mirrored ? " mirrored" : "",
                            hits[0].errors);
            std::printf("\n");
            CHECK(hits.size() == 1);
            if (!hits.empty())
            {
                CHECK(hits[0].code == c.code);
                CHECK(std::fabs(hits[0].centreHz - c.hz) < 6.0);
                CHECK(hits[0].mirrored == c.mirrored);
            }
        }
    }

    std::printf("\n=== range: a burst outside the range is not reported ===\n");
    {
        rsid::Detector d;
        d.setSampleRate(48000.0);
        d.setRange(1000.0, 2000.0);
        std::vector<float> x = rsid::makeBurst(69, 2500.0, 48000.0);
        CHECK(run(d, x, 1024).empty());
        d.reset();
        x = rsid::makeBurst(69, 1500.0, 48000.0);
        CHECK(run(d, x, 1024).size() == 1);
    }

    std::printf("\n=== no false IDs: 10 minutes of noise, PSK text, a steady carrier ===\n");
    {
        rsid::Detector d;
        d.setSampleRate(12000.0);
        std::vector<float> x(size_t(600 * 12000), 0.0f);
        addNoise(x, 0.3, 77);
        CHECK(run(d, x, 1200).empty());

        PskModulator m;
        m.setBaud(62.5);
        m.setCarrier(1500.0);
        m.preroll(1.0);
        for (int i = 0; i < 40; ++i)
            for (char c : std::string("CQ CQ DE KC9OXP KC9OXP EN71AR PSE K "))
                m.queueChar(c);
        m.tail(0.5);
        std::vector<float> p;
        std::vector<float> blk(1024);
        while (!m.idle())
        {
            m.render(blk.data(), blk.size(), 12000.0);
            p.insert(p.end(), blk.begin(), blk.end());
        }
        // plus a steady carrier
        for (size_t i = 0; i < p.size(); ++i)
            p[i] += 0.3f * float(std::sin(2.0 * M_PI * 800.0 * double(i) / 12000.0));
        addNoise(p, 0.1, 78);
        d.reset();
        std::printf("  %.0f s of BPSK63 text + carrier\n", double(p.size()) / 12000.0);
        CHECK(run(d, p, 1200).empty());
    }

    std::printf(g_fail ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
