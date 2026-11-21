// g++ -std=c++17 -Wall -Iengine tests/test_inputlevel.cpp -o test_inputlevel
// Top-bar RX input meter (engine/inputlevel.h): RMS/peak in dBFS, peak hold and colour zones.
#include "inputlevel.h"

#include <cmath>
#include <cstdio>
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

std::vector<float> sine(double amp, double hz, int n, double fs = 48000.0)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[size_t(i)] = float(amp * std::sin(2.0 * 3.14159265358979323846 * hz * i / fs));
    return v;
}

bool near(double a, double b, double tol = 0.2)
{
    return std::fabs(a - b) <= tol;
}
} // namespace

int main()
{
    std::printf("RMS and peak\n");
    {
        inlevel::Meter m;
        CHECK(m.empty());
        CHECK(m.rmsDb() <= -119.0);
        auto s = sine(1.0, 1000.0, 48000);
        m.add(s.data(), s.size());
        CHECK(near(m.rmsDb(), -3.01)); // full-scale sine: RMS -3 dBFS
        CHECK(near(m.peakDb(), 0.0));
        m.reset();
        auto q = sine(0.1, 1500.0, 48000); // -20 dB peak
        m.add(q.data(), 24000);
        m.add(q.data() + 24000, 24000); // two packets add up
        CHECK(near(m.rmsDb(), -23.01));
        CHECK(near(m.peakDb(), -20.0));
        m.reset();
        std::vector<float> zeros(4800, 0.0f);
        m.add(zeros.data(), zeros.size());
        CHECK(!m.empty());
        CHECK(m.rmsDb() <= -119.0); // all zeros: floor, not -inf
    }

    std::printf("Zones\n");
    CHECK(inlevel::zoneFor(-30.0, -20.0) == inlevel::Zone::Good);
    CHECK(inlevel::zoneFor(-55.0, -45.0) == inlevel::Zone::Quiet);
    CHECK(inlevel::zoneFor(-10.0, -4.0) == inlevel::Zone::Hot);
    CHECK(inlevel::zoneFor(-20.0, -0.5) == inlevel::Zone::Clip); // a clipped peak wins
    CHECK(inlevel::zoneFor(-120.0, -120.0) == inlevel::Zone::Quiet);

    std::printf("Bar position\n");
    CHECK(near(inlevel::fraction(-60.0), 0.0, 1e-9));
    CHECK(near(inlevel::fraction(-90.0), 0.0, 1e-9));
    CHECK(near(inlevel::fraction(-30.0), 0.5, 1e-9));
    CHECK(near(inlevel::fraction(0.0), 1.0, 1e-9));
    CHECK(near(inlevel::fraction(3.0), 1.0, 1e-9));

    std::printf("Peak hold\n");
    {
        inlevel::PeakHold h;
        CHECK(near(h.update(-10.0, 0.3), -10.0));
        CHECK(near(h.update(-30.0, 0.3), -10.0)); // held
        CHECK(near(h.update(-30.0, 0.3), -10.0));
        CHECK(near(h.update(-30.0, 0.3), -10.0)); // 0.9 s
        CHECK(near(h.update(-30.0, 0.3), -10.0)); // 1.2 s
        CHECK(near(h.update(-30.0, 0.3), -10.0)); // 1.5 s, still held
        CHECK(near(h.update(-30.0, 0.3), -16.0)); // then falls 20 dB/s
        CHECK(near(h.update(-30.0, 0.5), -26.0));
        CHECK(near(h.update(-30.0, 0.5), -30.0)); // never below the current peak
        CHECK(near(h.update(-5.0, 0.3), -5.0));   // new higher peak right away
    }

    if (g_fail)
    {
        std::printf("\n%d FAILED\n", g_fail);
        return 1;
    }
    std::printf("\nALL PASSED\n");
    return 0;
}
