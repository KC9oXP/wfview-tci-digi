// Qt-free tests of the software memories file format and the US licence-class rules.
// Build: g++ -std=c++17 -O2 -Wall -Iengine tests/test_memories.cpp -o test_memories
// Optional: test_memories <memories-builtin.csv> also checks the shipped list.

#include "memories.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

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

using namespace memories;

static unsigned cls(Emission e, double dialMHz, const char *sb)
{
    std::int64_t lo, hi;
    signalSpan(std::int64_t(dialMHz * 1e6 + 0.5), sb, lo, hi);
    return classesFor(e, lo, hi);
}

int main(int argc, char **argv)
{
    std::printf("parseDial / formatMHz\n");
    CHECK(parseDial("14.070") == 14070000);
    CHECK(parseDial(" 14.0705 ") == 14070500);
    CHECK(parseDial("14070000") == 14070000);
    CHECK(parseDial("14070 kHz") == 14070000);
    CHECK(parseDial("14.07 MHz") == 14070000);
    CHECK(parseDial("0.517") == 517000);
    CHECK(parseDial("7") == 7000000);
    CHECK(parseDial("14,070,000") == 14070000);
    CHECK(parseDial("abc") == 0);
    CHECK(parseDial("") == 0);
    CHECK(parseDial("10.1") == 10100000);
    CHECK(parseDial("4208.5 kHz") == 4208500);
    CHECK(formatMHz(14070000) == "14.070");
    CHECK(formatMHz(14070500) == "14.0705");
    CHECK(formatMHz(4208500) == "4.2085");
    CHECK(formatMHz(517000) == "0.517");
    CHECK(formatMHz(50293123) == "50.293123");

    std::printf("CSV\n");
    {
        const auto r = parseCsv("a,b,c\r\n\"x, y\",\"say \"\"hi\"\"\",\"two\nlines\"\r\n\r\nlast,,\n");
        CHECK(r.size() == 3);
        CHECK(r[1].size() == 3 && r[1][0] == "x, y" && r[1][1] == "say \"hi\"" && r[1][2] == "two\nlines");
        CHECK(r[2].size() == 3 && r[2][0] == "last" && r[2][1].empty() && r[2][2].empty());
        CHECK(csvField("plain") == "plain");
        CHECK(csvField("a,b") == "\"a,b\"");
        CHECK(csvField("say \"hi\"") == "\"say \"\"hi\"\"\"");
        CHECK(parseCsv("\xEF\xBB\xBFName,Tab\r\n").at(0).at(0) == "Name"); // Excel's BOM
    }

    std::printf("fromCsv / toCsv\n");
    {
        // Columns in another order, other header spellings, a bad row, a blank row.
        const std::string text = "comment,Freq (MHz),NAME,tab,sub mode,radio mode,License classes\n"
                                 "\"PSK, 20 m\",14.070,PSK31 20,PSK,BPSK31,usb,\n"
                                 "no freq,,Bad,PSK,,,\n"
                                 ",,,,,,\n"
                                 "SSTV,7.171,SSTV 40,SSTV,,lsb,Advanced Extra\n";
        int skipped = -1;
        const auto list = fromCsv(text, &skipped);
        CHECK(list.size() == 2);
        CHECK(skipped == 1);
        CHECK(list[0].name == "PSK31 20" && list[0].tab == "PSK" && list[0].subMode == "BPSK31");
        CHECK(list[0].dialHz == 14070000 && list[0].sideband == "USB" && list[0].comment == "PSK, 20 m");
        CHECK(list[1].sideband == "LSB" && list[1].classes == "Advanced Extra");
        const std::string out = toCsv(list);
        CHECK(out.rfind("Name,Tab,Sub-mode,Dial MHz,Sideband,Band,Classes,Comment\r\n", 0) == 0);
        const auto back = fromCsv(out);
        CHECK(back.size() == 2 && back[0].name == list[0].name && back[0].dialHz == list[0].dialHz
              && back[0].comment == list[0].comment && back[0].band == "20 m");
        CHECK(fromCsv("hello,world\n1,2\n").empty()); // not a memories file
    }

    std::printf("bands\n");
    CHECK(bandName(14070000) == "20 m");
    CHECK(bandName(3845000) == "80 m");
    CHECK(bandName(28680000) == "10 m");
    CHECK(bandName(4233100).empty());

    std::printf("classes: data (RTTY, PSK, ...)\n");
    const unsigned GAE = General | Advanced | Extra;
    const unsigned ALL = Novice | Technician | General | Advanced | Extra;
    CHECK(cls(Emission::Data, 14.070, "USB") == GAE);
    CHECK(cls(Emission::Data, 14.010, "USB") == Extra); // below 14.025
    CHECK(cls(Emission::Data, 3.580, "USB") == GAE);
    CHECK(cls(Emission::Data, 3.510, "USB") == Extra);
    CHECK(cls(Emission::Data, 7.080, "USB") == GAE);
    CHECK(cls(Emission::Data, 7.124, "USB") == 0); // signal runs past 7.125
    CHECK(cls(Emission::Data, 10.142, "USB") == GAE);
    CHECK(cls(Emission::Data, 18.100, "USB") == GAE);
    CHECK(cls(Emission::Data, 21.080, "USB") == GAE); // N/T: CW only on 15 m
    CHECK(cls(Emission::Data, 24.920, "USB") == GAE);
    CHECK(cls(Emission::Data, 28.120, "USB") == ALL); // 10 m data is for everyone
    CHECK(cls(Emission::Data, 28.400, "USB") == 0); // phone sub-band
    CHECK(cls(Emission::Data, 50.300, "USB") == (Technician | GAE)); // no 6 m for Novices
    CHECK(cls(Emission::Data, 1.805, "USB") == GAE);
    CHECK(cls(Emission::Data, 14.230, "USB") == 0);

    std::printf("classes: CW\n");
    CHECK(cls(Emission::Cw, 7.110, "USB") == ALL);
    CHECK(cls(Emission::Cw, 3.550, "USB") == ALL);
    CHECK(cls(Emission::Cw, 21.110, "USB") == ALL);
    CHECK(cls(Emission::Cw, 14.050, "USB") == GAE);
    CHECK(cls(Emission::Cw, 14.010, "USB") == Extra);
    CHECK(cls(Emission::Cw, 50.090, "USB") == (Technician | GAE));
    CHECK(cls(Emission::Cw, 7.020, "USB") == Extra);

    std::printf("classes: 60 m (channels and the 5351.5-5366.5 kHz segment)\n");
    CHECK(cls(Emission::Data, 5.3305, "USB") == GAE); // channel centred on 5332 kHz
    CHECK(cls(Emission::Data, 5.3465, "USB") == GAE);
    CHECK(cls(Emission::Data, 5.3715, "USB") == GAE);
    CHECK(cls(Emission::Data, 5.4035, "USB") == GAE);
    CHECK(cls(Emission::Data, 5.3520, "USB") == GAE); // the segment
    CHECK(cls(Emission::Data, 5.3635, "USB") == GAE); // top of the segment: 5363.5-5366.5
    CHECK(cls(Emission::Data, 5.3640, "USB") == 0); // runs past 5366.5
    CHECK(cls(Emission::Data, 5.3400, "USB") == 0); // between channels
    CHECK(cls(Emission::Data, 5.3570, "LSB") == GAE); // 5354.0-5357.0, inside the segment
    CHECK(cls(Emission::Cw, 5.3305, "USB") == GAE);
    CHECK(cls(Emission::Cw, 5.3400, "USB") == 0);
    CHECK(cls(Emission::Image, 5.3520, "USB") == 0); // no image emissions on 60 m

    std::printf("classes: SSTV (image)\n");
    CHECK(cls(Emission::Image, 3.845, "LSB") == GAE);
    CHECK(cls(Emission::Image, 7.171, "LSB") == (Advanced | Extra)); // General phone starts at 7.175
    CHECK(cls(Emission::Image, 14.230, "USB") == GAE);
    CHECK(cls(Emission::Image, 14.200, "USB") == (Advanced | Extra));
    CHECK(cls(Emission::Image, 21.340, "USB") == GAE);
    CHECK(cls(Emission::Image, 28.680, "USB") == GAE); // Technician phone ends at 28.5
    CHECK(cls(Emission::Image, 28.400, "USB") == (Novice | Technician | GAE));
    CHECK(cls(Emission::Image, 14.070, "USB") == 0);

    std::printf("classes text\n");
    {
        Memory m;
        m.tab = "PSK";
        m.dialHz = 28120000;
        m.sideband = "USB";
        CHECK(classesText(m) == "Novice, Technician, General, Advanced, Extra");
        m.tab = "NAVTEX";
        CHECK(classesText(m) == "Receive only");
        m.classes = " My own note ";
        CHECK(classesText(m) == "My own note");
        CHECK(parseClasses("Tech, General") == (Technician | General));
        CHECK(parseClasses("N T G A E") == ALL);
        CHECK(parseClasses("Amateur Extra") == Extra);
        CHECK(parseClasses("all") == ALL);
        CHECK(parseClasses("Receive only") == 0);
        CHECK(parseClasses("Other") == Other);
        CHECK(parseClasses("General, Other") == (General | Other));
        CHECK(classNames(General | Other) == "General, Other");
        CHECK(parseClasses("all") == ALL); // "all" is the five licence classes, not Other
    }

    if (argc > 1)
    {
        std::printf("shipped list %s\n", argv[1]);
        std::ifstream f(argv[1], std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        int skipped = -1;
        const auto list = fromCsv(ss.str(), &skipped);
        CHECK(list.size() >= 40);
        CHECK(skipped == 0);
        int bad = 0;
        for (const Memory &m : list)
        {
            // Every entry's Classes cell must match the rules (so the file and the app agree).
            Memory auto_ = m;
            auto_.classes.clear();
            if (classesText(auto_) != m.classes)
            {
                std::printf("  FAIL %s %s: file says \"%s\", rules say \"%s\"\n",
                            m.name.c_str(),
                            formatMHz(m.dialHz).c_str(),
                            m.classes.c_str(),
                            classesText(auto_).c_str());
                ++bad;
            }
            if (m.comment.empty())
            {
                std::printf("  FAIL %s has no comment\n", m.name.c_str());
                ++bad;
            }
        }
        CHECK(bad == 0);
    }

    std::printf("band column order\n");
    {
        CHECK(bandMetres("160 m") == 160 && bandMetres("60m") == 60 && bandMetres("70 cm") == 0.7);
        CHECK(bandMetres("") == 0 && bandMetres("HF") == 0);
        std::vector<std::pair<std::string, std::int64_t>> rows = {{"10 m", 28120000},
                                                                  {"160 m", 1807000},
                                                                  {"2 m", 144100000},
                                                                  {"17 m", 18100000},
                                                                  {"", 4625000},
                                                                  {"20 m", 14080000},
                                                                  {"20 m", 14070000},
                                                                  {"12 m", 24920000},
                                                                  {"6 m", 50290000},
                                                                  {"15 m", 21080000},
                                                                  {"60 m", 5332000},
                                                                  {"80 m", 3580000}};
        std::sort(rows.begin(),
                  rows.end(),
                  [](const auto &a, const auto &b) { return bandLess(a.first, a.second, b.first, b.second); });
        std::string order;
        for (const auto &r : rows)
            order += (r.first.empty() ? std::string("-") : r.first) + ",";
        std::printf("  %s\n", order.c_str());
        CHECK(order == "160 m,80 m,60 m,20 m,20 m,17 m,15 m,12 m,10 m,6 m,2 m,-,");
        CHECK(rows[3].second == 14070000); // same band: by dial
    }

    std::printf("Add built-in puts rows in place\n");
    {
        auto mk = [](const char *tab, std::int64_t hz)
        {
            Memory m;
            m.tab = tab;
            m.dialHz = hz;
            return m;
        };
        std::vector<Memory> l = {mk("CW", 1810000), mk("CW", 3560000), mk("CW", 7030000), mk("CW", 50090000),
                                 mk("RTTY", 3580000), mk("RTTY", 7080000)};
        insertInOrder(l, mk("CW", 5332000)); // 60 m: between 80 and 40
        CHECK(l[2].dialHz == 5332000 && l[3].dialHz == 7030000);
        insertInOrder(l, mk("RTTY", 1800000)); // below all RTTY rows: before the first
        CHECK(l[5].tab == "RTTY" && l[5].dialHz == 1800000);
        insertInOrder(l, mk("PSK", 14070000)); // no PSK rows: at the end
        CHECK(l.back().tab == "PSK");
        insertInOrder(l, mk("RTTY", 28080000)); // above all: after the last RTTY row
        CHECK(l[8].dialHz == 28080000 && l[9].tab == "PSK");
    }

    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
