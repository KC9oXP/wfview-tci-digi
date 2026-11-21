// g++ -std=c++17 -Wall -Iengine tests/test_stanagegg.cpp engine/rttycore.cpp -o test_stanagegg
// STANAG 4481 Easteregg (engine/stanagegg.h) on the real RTTY demodulator: a synchronous
// random 75 Bd / 850 Hz stream (like an encrypted broadcast) trips it once; amateur RTTY at the
// same settings, plain noise, other settings or the switch off do not.
#include "rttycore.h"
#include "stanagegg.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace
{
int g_fail = 0;
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (x)                                                                                                         \
            std::printf("  ok   %s\n", #x);                                                                            \
        else                                                                                                           \
        {                                                                                                              \
            std::printf("  FAIL %s (line %d)\n", #x, __LINE__);                                                        \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
    } while (0)

constexpr double kFs = 48000.0;
constexpr double kTwoPi = 6.283185307179586;
constexpr double kMark = 2125.0, kSpace = 1275.0; // 850 Hz shift

struct Gen
{
    std::mt19937 rng{7};
    double ph = 0.0;
    std::vector<float> v;
    void bit(bool mark, double bits)
    {
        std::normal_distribution<double> nd(0.0, 0.05);
        const int n = int(std::lround(bits / 75.0 * kFs));
        for (int i = 0; i < n; ++i)
        {
            ph += kTwoPi * (mark ? kMark : kSpace) / kFs;
            v.push_back(float(0.3 * std::sin(ph) + nd(rng)));
        }
    }
    void sync(double sec) // continuous random bits, no framing
    {
        std::uniform_int_distribution<int> b(0, 1);
        for (int i = 0; i < int(sec * 75.0); ++i)
            bit(b(rng) != 0, 1.0);
    }
    void async(double sec) // amateur-style characters: start, 5 data, 1.5 stop
    {
        std::uniform_int_distribution<int> b(0, 1);
        for (double t = 0.0; t < sec; t += 7.5 / 75.0)
        {
            bit(false, 1.0);
            for (int k = 0; k < 5; ++k)
                bit(b(rng) != 0, 1.0);
            bit(true, 1.5);
        }
    }
    void noise(double sec)
    {
        std::normal_distribution<double> nd(0.0, 0.1);
        for (int i = 0; i < int(sec * kFs); ++i)
            v.push_back(float(nd(rng)));
    }
};

// Runs the decoder over the audio in 150 ms steps (the app's stats interval); returns the
// times (s) the detector tripped.
std::vector<double> run(const std::vector<float> &v, bool enabled, double baud = 75.0, double shift = 850.0)
{
    RttyDemodulator d;
    RttyConfig c;
    c.sampleRate = kFs;
    c.baud = baud;
    c.shiftHz = shift;
    c.markHz = kMark;
    c.stopBits = 1.5;
    c.ita2 = true;
    d.setConfig(c);
    stanagegg::Detector det;
    std::vector<double> trips;
    const size_t step = size_t(0.15 * kFs);
    double t = 0.0;
    for (size_t o = 0; o < v.size(); o += step)
    {
        const size_t n = std::min(step, v.size() - o);
        d.process(v.data() + o, n);
        t += double(n) / kFs;
        const RttyDemodulator::Stats s = d.stats();
        if (det.update(enabled, baud, shift, s.quality, s.chars, s.framingErrors, double(n) / kFs))
            trips.push_back(t);
    }
    return trips;
}
} // namespace

int main()
{
    std::printf("Settings\n");
    CHECK(stanagegg::settingsMatch(75.0, 850.0));
    CHECK(!stanagegg::settingsMatch(45.45, 170.0));
    CHECK(!stanagegg::settingsMatch(75.0, 170.0));
    CHECK(!stanagegg::settingsMatch(100.0, 850.0));

    std::printf("Encrypted-style synchronous stream\n");
    {
        Gen g;
        g.sync(30.0);
        const auto trips = run(g.v, true);
        CHECK(trips.size() == 1); // once per lock, not every update
        CHECK(!trips.empty() && trips[0] >= 10.0 && trips[0] < 15.0);
        CHECK(run(g.v, false).empty()); // switch off
        CHECK(run(g.v, true, 45.45, 170.0).empty()); // not on other settings
    }

    std::printf("Amateur RTTY at 75 Bd / 850 Hz (stop bits always right)\n");
    {
        Gen g;
        g.async(30.0);
        CHECK(run(g.v, true).empty());
    }

    std::printf("Noise alone (fails the stop bit too, but no lock)\n");
    {
        Gen g;
        g.noise(30.0);
        CHECK(run(g.v, true).empty());
    }

    std::printf("Too short: 8 s of stream\n");
    {
        Gen g;
        g.sync(8.0);
        g.noise(10.0);
        CHECK(run(g.v, true).empty());
    }

    std::printf("Signal lost and back: trips again\n");
    {
        Gen g;
        g.sync(15.0);
        g.noise(6.0);
        g.sync(15.0);
        const auto trips = run(g.v, true);
        CHECK(trips.size() == 2);
    }

    std::printf("Counting starts at the lock\n");
    {
        stanagegg::Detector det;
        bool any = false;
        // 9 s locked, 20 chars all good, then 2 s with 10 bad: ratio over the lock 10/30
        for (int i = 0; i < 60; ++i)
            any |= det.update(true, 75.0, 850.0, 0.8, size_t(20 * i / 59), 0, 0.15);
        CHECK(!any);
        for (int i = 1; i <= 14; ++i)
            any |= det.update(true, 75.0, 850.0, 0.8, 20, size_t(i * 10 / 14), 0.15);
        CHECK(any);
    }

    std::printf("Message\n");
    CHECK(std::string(stanagegg::message()) == "ALL YOUR BASE ARE BELONG TO US");

    if (g_fail)
    {
        std::printf("\n%d FAILED\n", g_fail);
        return 1;
    }
    std::printf("\nALL PASSED\n");
    return 0;
}
