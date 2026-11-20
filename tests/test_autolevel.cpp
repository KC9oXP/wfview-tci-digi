// Qt-free tests for the auto TX level search (engine/autolevel.h).
// Build: g++ -std=c++17 -O2 -Wall -Iengine tests/test_autolevel.cpp -o test_autolevel

#include "autolevel.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                              \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
        else                                                                                                           \
            std::printf("  ok   %s\n", #cond);                                                                         \
    } while (0)

// A radio: no ALC below the knee, then rising 0.08 per dB, read to one decimal.
static double radioAlc(double db, double kneeDb)
{
    const double a = db <= kneeDb ? 0.0 : (db - kneeDb) * 0.08;
    return std::round(a * 10.0) / 10.0;
}

static autolevel::Search run(double startDb, double target, double kneeDb)
{
    autolevel::Search s(startDb, target);
    for (int i = 0; i < 40 && s.state() == autolevel::Search::State::Measuring; ++i)
        s.feed(radioAlc(s.currentDb(), kneeDb));
    return s;
}

int main()
{
    using St = autolevel::Search::State;
    std::printf("target 0.1, starting below the knee\n");
    {
        const auto s = run(-30.0, 0.1, -12.0);
        std::printf("  result %.1f dB, ALC %.1f, %d steps\n", s.resultDb(), s.resultAlc(), s.steps());
        CHECK(s.state() == St::Done);
        CHECK(radioAlc(s.resultDb(), -12.0) <= 0.1 + 1e-9);
        CHECK(radioAlc(s.resultDb() + 0.5, -12.0) > 0.1); // the next slider step up is over
        CHECK(s.steps() <= 10);
    }
    std::printf("target 0.1, starting far above the knee\n");
    {
        const auto s = run(0.0, 0.1, -25.0);
        std::printf("  result %.1f dB, %d steps\n", s.resultDb(), s.steps());
        CHECK(s.state() == St::Done);
        CHECK(radioAlc(s.resultDb(), -25.0) <= 0.1 + 1e-9);
        CHECK(radioAlc(s.resultDb() + 0.5, -25.0) > 0.1);
    }
    std::printf("target 0: find ALC first, then back off to none\n");
    {
        autolevel::Search s(-30.0, 0.0);
        bool sawAlc = false;
        for (int i = 0; i < 40 && s.state() == St::Measuring; ++i)
        {
            const double a = radioAlc(s.currentDb(), -12.0);
            sawAlc = sawAlc || a > 0.0;
            s.feed(a);
        }
        std::printf("  result %.1f dB, %d steps\n", s.resultDb(), s.steps());
        CHECK(sawAlc); // it had to go over first
        CHECK(s.state() == St::Done);
        CHECK(radioAlc(s.resultDb(), -12.0) == 0.0);
        CHECK(radioAlc(s.resultDb() + 0.5, -12.0) > 0.0);
    }
    std::printf("never any ALC: stops at full level\n");
    {
        const auto s = run(-20.0, 0.1, 10.0);
        CHECK(s.state() == St::AtMaxBelow);
        CHECK(s.resultDb() == 0.0);
    }
    std::printf("ALC over the target even at the bottom\n");
    {
        const auto s = run(-10.0, 0.1, -60.0);
        CHECK(s.state() == St::AtMinOver);
    }
    std::printf("from the bottom with the knee near full level: 6 dB steps while ALC reads 0\n");
    {
        const auto s = run(-40.0, 0.1, -2.0);
        std::printf("  result %.1f dB, %d steps\n", s.resultDb(), s.steps());
        CHECK(s.state() == St::Done);
        CHECK(s.haveResult());
        CHECK(radioAlc(s.resultDb(), -2.0) <= 0.1 + 1e-9);
        CHECK(radioAlc(s.resultDb() + 0.5, -2.0) > 0.1);
        CHECK(s.steps() <= 10);
    }
    std::printf("a level of 0 (bottom) starts from -20 dB\n");
    {
        autolevel::Search s(txlevel::kMinDb, 0.1);
        CHECK(s.currentDb() == -20.0);
    }
    std::printf(g_fail ? "FAILED: %d\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
