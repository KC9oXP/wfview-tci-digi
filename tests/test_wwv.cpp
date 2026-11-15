// Qt-free tests of the WWV / WWVH receiver: frame code, tick tracking and
// the audio clock error, time decode with noise, tones, carrier measurement.
// Build: g++ -std=c++17 -O2 -Iengine tests/test_wwv.cpp engine/wwvcore.cpp -o test_wwv

#include "wwvcore.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

struct Result
{
    WwvDecoder::Status st;
    double firstVerifiedSec = -1;
    double startSample = 0; // where the encoder's first second began
};

static Result run(const wwv::Time &t0,
                  int startSec,
                  double seconds,
                  double ppm,
                  double noise,
                  bool hawaii,
                  unsigned seed,
                  double fs = 48000.0,
                  double lead = 0.37)
{
    WwvEncoder enc;
    enc.start(t0, startSec, hawaii);
    WwvDecoder dec;
    dec.setSampleRate(fs);
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.0f, float(noise));
    std::vector<float> block(960);
    Result r;
    // some noise first, so the first tick is not at sample 0
    const size_t leadN = size_t(lead * fs);
    r.startSample = double(leadN);
    for (size_t done = 0; done < leadN;)
    {
        const size_t n = std::min(block.size(), leadN - done);
        for (size_t i = 0; i < n; ++i)
            block[i] = nd(rng);
        dec.process(block.data(), n);
        done += n;
    }
    // A sound card that is fast by ppm takes more samples in a second.
    const double txRate = fs * (1.0 + ppm * 1e-6);
    for (double t = 0; t < seconds; t += double(block.size()) / fs)
    {
        enc.render(block.data(), block.size(), txRate);
        for (float &v : block)
            v += nd(rng);
        dec.process(block.data(), block.size());
        if (r.firstVerifiedSec < 0 && dec.status().timeVerified)
            r.firstVerifiedSec = t;
    }
    r.st = dec.status();
    return r;
}

int main()
{
    std::printf("Frame code\n");
    {
        wwv::Time t;
        t.year = 2026;
        t.dayOfYear = 319;
        t.hour = 22;
        t.minute = 17;
        t.dut1 = -0.3;
        t.dst1 = false;
        t.dst2 = false;
        const std::string s = wwv::encodeFrame(t);
        std::printf("  %s\n", s.c_str());
        // minute 17: units 7 = seconds 10,11,12; tens 10 = second 15
        CHECK(s.substr(9, 11) == "M111001000M");
        CHECK(s[9] == 'M' && s[59] == 'M' && s[0] == '-');
        // year 26: units 6 = seconds 5,6; tens 20 = second 52
        CHECK(s[4] == '0' && s[5] == '1' && s[6] == '1' && s[7] == '0' && s[51] == '0' && s[52] == '1');
        // day 319: 9 = 30,33; 10 = 35; 300 = 40,41
        CHECK(s[30] == '1' && s[33] == '1' && s[35] == '1' && s[40] == '1' && s[41] == '1' && s[31] == '0');
        // DUT1 -0.3: sign 0, weights 0.1 + 0.2
        CHECK(s[50] == '0' && s[56] == '1' && s[57] == '1' && s[58] == '0');
        wwv::Time u;
        CHECK(wwv::decodeFrame(s, u));
        CHECK(u == t && std::fabs(u.dut1 + 0.3) < 1e-9);
        std::string bad = s;
        bad[19] = '0'; // a marker missing
        CHECK(!wwv::decodeFrame(bad, u));
        bad = s;
        bad[14] = '1'; // an unused bit set
        CHECK(!wwv::decodeFrame(bad, u));
        bad = s;
        bad[21] = bad[23] = '1'; // hour units 2 + 2 + 8 = 12 > 9
        CHECK(!wwv::decodeFrame(bad, u));
    }

    std::printf("Path delay\n");
    {
        double lat = 0, lon = 0, km = 0;
        CHECK(wwv::gridToLatLon("EN71ar", lat, lon));
        const double ms = wwv::pathDelayMs(lat, lon, false, &km);
        std::printf("  EN71ar: %.2f N %.2f E, %.0f km, %.2f ms\n", lat, lon, km, ms);
        CHECK(std::fabs(lat - 41.73) < 0.05 && std::fabs(lon + 85.96) < 0.05);
        CHECK(km > 1500 && km < 1750 && ms > 5.0 && ms < 6.5);
        CHECK(!wwv::gridToLatLon("ZZ", lat, lon));
    }

    wwv::Time t0;
    t0.year = 2026;
    t0.dayOfYear = 319;
    t0.hour = 23;
    t0.minute = 58; // runs over the hour (1500 Hz minute tone) and into the next day
    t0.dut1 = 0.1;
    t0.dst1 = t0.dst2 = false;

    std::printf("Clean signal, sound card 37 ppm fast, joined at second 41\n");
    {
        const Result r = run(t0, 41, 200.0, 37.0, 0.01, false, 1);
        const auto &s = r.st;
        std::printf("  ticks %d code %d tones %d (%d Hz, %+.3f Hz) valid %d verified %d after %.0f s\n",
                    s.ticks,
                    s.code,
                    s.tones,
                    s.toneHz,
                    s.toneErrorHz,
                    s.timeValid,
                    s.timeVerified,
                    r.firstVerifiedSec);
        std::printf("  clock %+.1f ppm over %.0f s, jitter %.3f ms; time %04d day %d %02d:%02d:%02d\n",
                    s.clockPpm,
                    s.clockPpmSpanSec,
                    s.tickJitterMs,
                    s.time.year,
                    s.time.dayOfYear,
                    s.time.hour,
                    s.time.minute,
                    s.secondInMinute);
        CHECK(s.ticks && s.code && s.timeValid && s.timeVerified && !s.hawaii);
        CHECK(std::fabs(s.clockPpm - 37.0) < 1.5);
        CHECK(s.tickJitterMs < 0.15);
        // 41 + 200 s after 23:58:00 = 00:02:01 next day (last full second begun: about :01)
        CHECK(s.time.dayOfYear == 320 && s.time.hour == 0 && s.time.minute == 2);
        CHECK(s.secondInMinute >= 0 && s.secondInMinute <= 1);
        // The tick sample is where that second began: start + elapsed seconds at the fast rate.
        const double elapsed = (2 * 60 + s.secondInMinute) + (60 - 41) + 60; // 23:58:41 -> 00:02:ss
        const double want = r.startSample + elapsed * 48000.0 * (1.0 + 37e-6);
        std::printf("  tick sample %.1f, expected %.1f (%.3f ms)\n", s.tickSample, want, (s.tickSample - want) / 48.0);
        CHECK(std::fabs(s.tickSample - want) < 0.0005 * 48000.0);
        CHECK(r.firstVerifiedSec > 0 && r.firstVerifiedSec < 150);
    }

    std::printf("Noisy (noise 0.2 against ticks 0.5), -80 ppm, WWVH, 44.1 kHz\n");
    {
        const Result r = run(t0, 10, 260.0, -80.0, 0.2, true, 7, 44100.0);
        const auto &s = r.st;
        std::printf("  ticks %d code %d valid %d verified %d frames %d clock %+.1f ppm jitter %.3f ms hawaii %d\n",
                    s.ticks,
                    s.code,
                    s.timeValid,
                    s.timeVerified,
                    s.framesDecoded,
                    s.clockPpm,
                    s.tickJitterMs,
                    s.hawaii);
        std::printf("  symbols %s\n", s.symbols.c_str());
        CHECK(s.ticks && s.code && s.timeVerified && s.hawaii);
        CHECK(std::fabs(s.clockPpm + 80.0) < 4.0);
        CHECK(s.time.hour == 0 && s.time.dayOfYear == 320);
    }

    std::printf("Several noise seeds (they differ by compiler): time verified within 3 minutes\n");
    {
        int good = 0, runs = 0;
        for (unsigned seed : {11u, 12u, 13u, 14u})
        {
            const Result r = run(t0, 25, 185.0, 12.0, 0.1, false, seed);
            good += r.st.timeVerified && std::fabs(r.st.clockPpm - 12.0) < 3.0;
            ++runs;
        }
        CHECK(good == runs);
    }

    std::printf("No 100 Hz (a radio whose audio does not pass it): ticks and clock work, code lamp off, no time\n");
    {
        WwvEncoder enc;
        enc.start(t0, 0, false);
        enc.setCodeLevel(0.0);
        WwvDecoder dec;
        dec.setSampleRate(48000.0);
        std::vector<float> block(960);
        std::mt19937 rng(3);
        std::normal_distribution<float> nd(0.0f, 0.05f);
        for (int b = 0; b < 50 * 150; ++b)
        {
            enc.render(block.data(), block.size(), 48000.0);
            for (float &v : block)
                v += nd(rng);
            dec.process(block.data(), block.size());
        }
        const auto &s = dec.status();
        std::printf("  ticks %d code %d valid %d clock %+.1f ppm\n", s.ticks, s.code, s.timeValid, s.clockPpm);
        CHECK(s.ticks && std::fabs(s.clockPpm) < 2.0);
        CHECK(!s.code && !s.timeValid);
    }

    std::printf("Noise only: nothing lights\n");
    {
        WwvDecoder dec;
        dec.setSampleRate(48000.0);
        std::mt19937 rng(5);
        std::normal_distribution<float> nd(0.0f, 0.2f);
        std::vector<float> block(960);
        for (int b = 0; b < 50 * 60; ++b)
        {
            for (float &v : block)
                v = nd(rng);
            dec.process(block.data(), block.size());
        }
        const auto &s = dec.status();
        std::printf("  ticks %d code %d tones %d valid %d\n", s.ticks, s.code, s.tones, s.timeValid);
        CHECK(!s.code && !s.tones && !s.timeValid);
    }

    std::printf("Tones: 600 Hz read 0.25 Hz high\n");
    {
        WwvDecoder dec;
        dec.setSampleRate(48000.0);
        std::mt19937 rng(9);
        std::normal_distribution<float> nd(0.0f, 0.05f);
        std::vector<float> block(960);
        double ph = 0;
        for (int b = 0; b < 50 * 12; ++b)
        {
            for (float &v : block)
            {
                ph += 2 * M_PI * 600.25 / 48000.0;
                v = float(0.25 * std::sin(ph)) + nd(rng);
            }
            dec.process(block.data(), block.size());
        }
        const auto &s = dec.status();
        std::printf("  tone %d Hz %+.3f Hz\n", s.toneHz, s.toneErrorHz);
        CHECK(s.tones && s.toneHz == 600 && std::fabs(s.toneErrorHz - 0.25) < 0.03);
    }

    std::printf("Carrier mode: carrier at 1000 Hz +3.7 Hz and -41.2 Hz with modulation and noise\n");
    for (double off : {3.7, -41.2})
    {
        WwvDecoder dec;
        dec.setSampleRate(48000.0);
        dec.setCarrierMode(true);
        std::mt19937 rng(21);
        std::normal_distribution<float> nd(0.0f, 0.1f);
        std::vector<float> block(960);
        double ph = 0, ph2 = 0;
        for (int b = 0; b < 50 * 15; ++b)
        {
            for (float &v : block)
            {
                ph += 2 * M_PI * (1000.0 + off) / 48000.0;
                ph2 += 2 * M_PI * (1500.0 + off) / 48000.0; // the 500 Hz tone's upper sideband
                v = float(0.3 * std::sin(ph) + 0.08 * std::sin(ph2)) + nd(rng);
            }
            dec.process(block.data(), block.size());
        }
        const auto &s = dec.status();
        std::printf("  carrier %d at %.3f Hz (%.0f s)\n", s.carrier, s.carrierHz, s.carrierSeconds);
        CHECK(s.carrier && std::fabs(s.carrierHz - (1000.0 + off)) < 0.05);
    }
    std::printf("Carrier mode: noise only finds no carrier\n");
    {
        WwvDecoder dec;
        dec.setSampleRate(48000.0);
        dec.setCarrierMode(true);
        std::mt19937 rng(22);
        std::normal_distribution<float> nd(0.0f, 0.2f);
        std::vector<float> block(960);
        for (int b = 0; b < 50 * 15; ++b)
        {
            for (float &v : block)
                v = nd(rng);
            dec.process(block.data(), block.size());
        }
        CHECK(!dec.status().carrier);
    }

    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
