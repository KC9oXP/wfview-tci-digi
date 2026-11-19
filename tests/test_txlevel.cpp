// Qt-free tests of the dB TX level slider mapping (GitHub issue #2).
// Build: g++ -std=c++17 -O2 -Wall -Iengine tests/test_txlevel.cpp -o test_txlevel

#include "txlevel.h"

#include <cmath>
#include <cstdio>

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

static bool near(double a, double b, double tol = 1e-9)
{
    return std::fabs(a - b) <= tol;
}

int main()
{
    using namespace txlevel;
    std::printf("dB <-> level\n");
    CHECK(near(dbToLevel(0.0), 1.0));
    CHECK(near(dbToLevel(-6.0), 0.501187, 1e-6));
    CHECK(near(dbToLevel(-20.0), 0.1));
    CHECK(near(dbToLevel(-40.0), 0.01));
    CHECK(near(dbToLevel(-60.0), 0.01)); // clamped to the bottom
    CHECK(near(dbToLevel(+3.0), 1.0)); // never above full level
    CHECK(near(levelToDb(1.0), 0.0));
    CHECK(near(levelToDb(0.1), -20.0));
    CHECK(near(levelToDb(0.0), kMinDb));
    CHECK(near(levelToDb(-1.0), kMinDb));
    CHECK(near(levelToDb(2.0), 0.0));

    std::printf("old percent settings land where expected\n");
    CHECK(levelToSteps(1.00) == 0); // 100% -> 0 dB
    CHECK(levelToSteps(0.50) == -12); // 50%  -> -6.0 dB
    CHECK(levelToSteps(0.04) == -56); // 4%   -> -28.0 dB (IC-7610 setting)
    CHECK(levelToSteps(0.01) == -80); // 1%   -> -40 dB (old SSTV minimum)
    CHECK(levelToSteps(0.0) == kMinSteps);

    std::printf("every slider step round-trips\n");
    bool all = true;
    for (int s = kMinSteps; s <= kMaxSteps; ++s)
        if (levelToSteps(stepsToLevel(s)) != s)
            all = false;
    CHECK(all);
    CHECK(near(stepsToDb(-55), -27.5));
    CHECK(near(stepsToDb(-200), kMinDb));

    std::printf("adjacent steps are 0.5 dB apart everywhere\n");
    bool even = true;
    for (int s = kMinSteps; s < kMaxSteps; ++s)
        if (!near(levelToDb(stepsToLevel(s + 1)) - levelToDb(stepsToLevel(s)), 0.5, 1e-9))
            even = false;
    CHECK(even);

    std::printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
