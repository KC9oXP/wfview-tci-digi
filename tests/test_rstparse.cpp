// Qt-free tests of the RST rcvd finder (engine/rstparse.h).
// Build: g++ -std=c++17 -O2 -Wall -Iengine tests/test_rstparse.cpp -o test_rstparse
#include "rstparse.h"

#include <cstdio>

static int g_fail = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (cond)                                                                                                      \
            std::printf("  ok   %s\n", #cond);                                                                         \
        else                                                                                                           \
        {                                                                                                              \
            std::printf("  FAIL %s  (line %d)\n", #cond, __LINE__);                                                    \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
    } while (0)

static std::string rep(const char *text, const char *their = "")
{
    const rpt::Found f = rpt::findReport(text, "KC9OXP", their);
    std::printf("  \"%s\" -> \"%s\" (%s)\n", text, f.report.c_str(), f.context.c_str());
    return f.report;
}

int main()
{
    std::printf("Reports sent to us\n");
    CHECK(rep("KC9OXP DE K1LNF UR RST 599 599 BTU") == "599");
    CHECK(rep("kc9oxp de k1lnf ur rst is 579 579 hw?") == "579");
    CHECK(rep("KC9OXP DE W1AW TU UR 5NN 5NN K") == "599");
    CHECK(rep("KC9OXP DE W1AW RST 4N9") == "499");
    CHECK(rep("KC9OXP/P DE W1AW UR RPT 559") == "559");
    CHECK(rep("KC9OXP de K1LNF\nGM OM, UR RST: 589 589 in Inman SC") == "589");
    CHECK(rep("KC9OXP DE K1LNF UR 59") == "59");
    std::printf("The latest one wins\n");
    CHECK(rep("KC9OXP DE K1LNF UR 579 ... KC9OXP DE K1LNF CORRECTION UR 589") == "589");
    std::printf("Not for us, or no report\n");
    CHECK(rep("W1AW DE K1LNF UR RST 599 599").empty());
    CHECK(rep("CQ CQ DE K1LNF K1LNF PSE K").empty());
    CHECK(rep("KC9OXP DE K1LNF UR QTH IS INMAN").empty());
    CHECK(rep("KC9OXP DE K1LNF UR RST 699").empty()); // R is 1..5
    CHECK(rep("KC9OXP DE K1LNF UR RST 509").empty()); // S is 1..9
    CHECK(rep("KC9OXP DE K1LNF UR NNN").empty());
    CHECK(rep("KC9OXP DE K1LNF TNX FER 599 RPT").empty()); // number before the word
    std::printf("Their call, when known, must be there too\n");
    CHECK(rep("KC9OXP DE K1LNF UR 599", "K1LNF") == "599");
    CHECK(rep("KC9OXP DE N0ABC UR 599", "K1LNF").empty());
    std::printf("Our call too far back (more than 40 words)\n");
    {
        std::string t = "KC9OXP DE K1LNF";
        for (int i = 0; i < 45; ++i)
            t += " BLA";
        t += " UR 599";
        CHECK(rep(t.c_str()).empty());
    }
    std::printf("No call of ours set: nothing\n");
    CHECK(rpt::findReport("KC9OXP DE K1LNF UR 599", "", "").report.empty());
    std::printf("Base calls\n");
    CHECK(rpt::detail::baseCall("VE3/KC9OXP") == "KC9OXP");
    CHECK(rpt::detail::baseCall("kc9oxp/qrp") == "KC9OXP");

    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
