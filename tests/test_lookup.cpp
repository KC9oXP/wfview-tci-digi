// Qt-free tests of the callsign lookup parsers (QRZ XML, HamQTH) and the signal report
// (SNR from the spectrum, S-meter in S units) for the QSO log.
// Build: g++ -std=c++17 -O2 -Wall -Iengine tests/test_lookup.cpp -o test_lookup

#include "lookupcore.h"
#include "signalreport.h"

#include <cmath>
#include <cstdio>
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

int main()
{
    using namespace lookup;

    std::printf("QRZ: login\n");
    {
        const std::string xml
            = "<?xml version=\"1.0\" ?>\n<QRZDatabase version=\"1.34\" xmlns=\"http://xmldata.qrz.com\">"
              "<Session><Key>2331uf894c4bd29f3923f3bacf02c532d7bd9</Key><Count>123</Count>"
              "<SubExp>Wed Jan 1 12:34:03 2027</SubExp><GMTime>Sun Nov 16 04:13:46 2026</GMTime>"
              "</Session></QRZDatabase>";
        const Result r = parseQrz(xml);
        CHECK(r.session == "2331uf894c4bd29f3923f3bacf02c532d7bd9");
        CHECK(!r.found && r.error.empty() && !r.loginFailed && !r.sessionBad);
    }
    {
        const Result r = parseQrz("<QRZDatabase><Session><Error>Username/password incorrect</Error>"
                                  "<GMTime>x</GMTime></Session></QRZDatabase>");
        CHECK(r.loginFailed);
        CHECK(r.session.empty());
    }
    {
        const Result r = parseQrz("<QRZDatabase><Session><Error>Session Timeout</Error></Session></QRZDatabase>");
        CHECK(r.sessionBad && !r.loginFailed);
        const Result r2 = parseQrz("<QRZDatabase><Session><Error>Invalid session key</Error></Session></QRZDatabase>");
        CHECK(r2.sessionBad);
    }

    std::printf("QRZ: lookup (full record, US)\n");
    {
        const std::string xml = "<QRZDatabase version=\"1.34\"><Callsign><call>KD8PE</call><fname>HERMAN C</fname>"
                                "<name>MCCREARY</name><addr1>1 Main St</addr1><addr2>LONDON</addr2><state>KY</state>"
                                "<country>United States</country><grid>EM87ab</grid></Callsign>"
                                "<Session><Key>abc</Key></Session></QRZDatabase>";
        const Result r = parseQrz(xml);
        CHECK(r.found);
        CHECK(r.call == "KD8PE");
        CHECK(r.name == "Herman C");
        CHECK(r.qth == "London, KY");
        CHECK(r.grid == "EM87ab");
        CHECK(r.session == "abc");
    }
    std::printf("QRZ: nickname wins, DX country, entities\n");
    {
        const std::string xml = "<QRZDatabase><Callsign><call>g4abc</call><fname>JOHN</fname><nickname>Jack</nickname>"
                                "<addr2>St Albans &amp; District</addr2><country>England</country></Callsign>"
                                "<Session><Key>k</Key><Message>A subscription is required to access the complete "
                                "record.</Message></Session></QRZDatabase>";
        const Result r = parseQrz(xml);
        CHECK(r.found && r.call == "G4ABC");
        CHECK(r.name == "Jack");
        CHECK(r.qth == "St Albans & District, England");
        CHECK(r.grid.empty());
        CHECK(r.message.find("subscription") != std::string::npos);
    }
    {
        const Result r = parseQrz("<QRZDatabase><Session><Error>Not found: g1srdd</Error><Key>k</Key></Session>"
                                  "</QRZDatabase>");
        CHECK(!r.found && !r.sessionBad && !r.loginFailed && r.error == "Not found: g1srdd");
    }
    std::printf("QRZ: query strings are encoded\n");
    CHECK(qrzLoginQuery("kc9oxp", "p&ss word", "wfview-tci-digi/0.9.0")
          == "username=kc9oxp&password=p%26ss%20word&agent=wfview-tci-digi%2F0.9.0");
    CHECK(qrzLookupQuery("k;1", "W1AW/P") == "s=k%3B1&callsign=W1AW%2FP");

    std::printf("HamQTH\n");
    {
        const std::string login = "<?xml version=\"1.0\"?><HamQTH version=\"2.7\" xmlns=\"https://www.hamqth.com\">"
                                  "<session><session_id>09b0ae90050be03c452ad235a1f2915ad684393c</session_id>"
                                  "</session></HamQTH>";
        const Result r = parseHamqth(login);
        CHECK(r.session == "09b0ae90050be03c452ad235a1f2915ad684393c");
        CHECK(!r.found);
        const Result bad
            = parseHamqth("<HamQTH><session><error>Wrong user name or password</error></session></HamQTH>");
        CHECK(bad.loginFailed);
        const Result exp
            = parseHamqth("<HamQTH><session><error>Session does not exist or expired</error></session></HamQTH>");
        CHECK(exp.sessionBad && !exp.loginFailed);
        const Result nf = parseHamqth("<HamQTH><session><error>Callsign not found</error></session></HamQTH>");
        CHECK(!nf.found && !nf.sessionBad && !nf.loginFailed);

        const std::string xml = "<HamQTH version=\"2.7\"><search><callsign>ok2cqr</callsign><nick>Petr</nick>"
                                "<qth>Neratovice</qth><country>Czech Republic</country><grid>jo70gg</grid>"
                                "<adr_name>Petr Hlozek</adr_name></search></HamQTH>";
        const Result s = parseHamqth(xml);
        CHECK(s.found && s.call == "OK2CQR");
        CHECK(s.name == "Petr");
        CHECK(s.qth == "Neratovice, Czech Republic");
        CHECK(s.grid == "jo70gg");
        const Result us = parseHamqth("<HamQTH><search><callsign>W1AW</callsign><adr_name>ARRL HQ</adr_name>"
                                      "<adr_city>NEWINGTON</adr_city><us_state>CT</us_state>"
                                      "<country>United States</country></search></HamQTH>");
        CHECK(us.name == "Arrl Hq");
        CHECK(us.qth == "Newington, CT");
    }
    CHECK(hamqthLookupQuery("id1", "VE3/W1AW", "wfview-tci-digi") == "id=id1&callsign=VE3%2FW1AW&prg=wfview-tci-digi");

    std::printf("base call\n");
    CHECK(baseCall("w1aw/p") == "W1AW");
    CHECK(baseCall("VE3/W1AW") == "W1AW");
    CHECK(baseCall("EA8/G4ABC/P") == "G4ABC");
    CHECK(baseCall("KC9OXP") == "KC9OXP");

    std::printf("tagText edge cases\n");
    CHECK(tagText("<a><b/><b>x</b></a>", "b").empty()); // first <b> is empty
    CHECK(tagText("<h:nick>Al</h:nick>", "nick") == "Al");
    CHECK(tagText("<grid>", "grid").empty());
    CHECK(xmlDecode("J&#246;rg &lt;x&gt;") == "J\xC3\xB6rg <x>");

    std::printf("S-meter\n");
    using namespace sigrep;
    CHECK(sUnits(-73.0) == "S9");
    CHECK(sUnits(-61.0) == "S9+12");
    CHECK(sUnits(-85.0) == "S7");
    CHECK(sUnits(-121.0) == "S1");
    CHECK(sUnits(-140.0) == "S0");
    CHECK(note(18.4, -85.0) == "SNR 18 dB, S7");
    CHECK(note(std::nan(""), -85.0) == "S7");
    CHECK(note(-3.0, std::nan("")) == "SNR -3 dB");
    CHECK(note(std::nan(""), std::nan("")).empty());

    std::printf("SNR from the spectrum\n");
    {
        const double hzPerBin = 46.875 / 4; // 4096-point FFT at 48 kHz
        const int n = 2048;
        std::vector<float> db(n, -100.0f);
        for (int i = 0; i < n; ++i)
            db[size_t(i)] += float((i * 7) % 5) - 2.0f; // a little ripple around -100
        const int bin = int(1500.0 / hzPerBin);
        db[size_t(bin)] = -80.0f; // 20 dB above the floor at 1500 Hz
        const double s = snrDb(db.data(), n, hzPerBin, 1500.0, 50.0);
        std::printf("  measured %.1f dB\n", s);
        CHECK(std::fabs(s - 20.0) < 2.5);
        CHECK(std::isnan(snrDb(db.data(), n, hzPerBin, 0.0, 50.0))); // no signal frequency
        CHECK(snrDb(db.data(), n, hzPerBin, 2200.0, 50.0) < 4.0); // nothing there
    }

    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
