// Qt-free tests of the version comparison used by the update check.
// Build: g++ -std=c++17 -O2 -Wall -Iengine tests/test_version.cpp -o test_version

#include "versioncheck.h"

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
    using namespace versioncheck;
    std::printf("parse\n");
    CHECK((parse("0.5.3") == std::vector<int>{0, 5, 3}));
    CHECK((parse("v0.5.3-alpha") == std::vector<int>{0, 5, 3}));
    CHECK((parse("V1.0") == std::vector<int>{1, 0}));
    CHECK((parse(" v0.10.0") == std::vector<int>{0, 10, 0}));
    CHECK((parse("v2") == std::vector<int>{2}));
    CHECK((parse("0.5.3.") == std::vector<int>{0, 5, 3}));
    CHECK(parse("test").empty());
    CHECK(parse("").empty());
    CHECK(parse("alpha-0.5").empty());
    CHECK(toString(parse("v0.5.3-alpha")) == "0.5.3");

    std::printf("compare\n");
    CHECK(compare(parse("0.5.3"), parse("0.5.2")) > 0);
    CHECK(compare(parse("0.5.2"), parse("0.5.3")) < 0);
    CHECK(compare(parse("0.5"), parse("0.5.0")) == 0);
    CHECK(compare(parse("0.10.0"), parse("0.9.9")) > 0); // numbers, not text
    CHECK(compare(parse("1.0"), parse("0.99.99")) > 0);

    std::printf("isNewer\n");
    CHECK(isNewer("v0.5.3-alpha", "0.5.2"));
    CHECK(!isNewer("v0.5.2-alpha", "0.5.2"));
    CHECK(!isNewer("v0.5.0-alpha", "0.5.2"));
    CHECK(isNewer("v0.6.0", "0.5.9"));
    CHECK(isNewer("v1.0.0-beta", "0.5.2"));
    CHECK(!isNewer("nightly", "0.5.2")); // not a version: never offered
    CHECK(!isNewer("v0.5.3-alpha", "test")); // a test build never sees an update

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
