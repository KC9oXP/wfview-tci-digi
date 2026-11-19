// Qt-free tests of the frequency-box digit tuning.
// Build: g++ -std=c++17 -O2 -Wall -Iengine tests/test_freqdigits.cpp -o test_freqdigits

#include "freqdigits.h"

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

int main()
{
    using namespace freqdigits;
    const std::string t = "14.074.000";
    std::printf("placeAt\n");
    CHECK(placeAt(t, 0) == 10000000);
    CHECK(placeAt(t, 1) == 1000000);
    CHECK(placeAt(t, 2) == 0); // the dot
    CHECK(placeAt(t, 3) == 100000);
    CHECK(placeAt(t, 5) == 1000);
    CHECK(placeAt(t, 9) == 1);
    CHECK(placeAt(t, 10) == 0);
    CHECK(placeAt(t, -1) == 0);
    CHECK(placeAt("4.999.300", 0) == 1000000);
    CHECK(placeAt("", 0) == 0);

    std::printf("step\n");
    CHECK(step(4999300, 1000, 1) == 5000300); // carries into the MHz digit
    CHECK(step(5000000, 100, -7) == 4999300);
    CHECK(step(14074000, 1, -1) == 14073999);
    CHECK(step(500, 1000, -1) == 1); // never 0 or below
    CHECK(step(999999000, 1000000, 1) == kMaxHz);

    std::printf("format (zero filled)\n");
    CHECK(format(1810000) == "001.810.000");
    CHECK(format(14074000) == "014.074.000");
    CHECK(format(144200000) == "144.200.000");
    CHECK(format(0) == "000.000.000");
    CHECK(format(2000000000) == "999.999.999");
    CHECK(placeAt(format(1810000), 1) == 10000000); // the 10 MHz digit is there to turn
    CHECK(step(1810000, placeAt(format(1810000), 1), 1) == 11810000);
    CHECK(leadingZeros("001.810.000") == 2);
    CHECK(leadingZeros("014.074.000") == 1);
    CHECK(leadingZeros("000.517.000") == 4); // "000." dim
    CHECK(leadingZeros("144.200.000") == 0);
    CHECK(leadingZeros("000.000.000") == 11);

    std::printf(g_fail ? "\n%d FAILED\n" : "\nall passed\n", g_fail);
    return g_fail ? 1 : 0;
}
