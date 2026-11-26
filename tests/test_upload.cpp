// Qt-free tests of the log-upload helpers (QRZ form encoding and replies, TQSL commands).
// Build: g++ -std=c++17 -O2 -Wall -Iengine tests/test_upload.cpp -o test_upload

#include "uploadcore.h"

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

using namespace upload;

int main()
{
    std::printf("form encoding: nothing can break out of its field\n");
    CHECK(formEncode("ABC-123_x.~") == "ABC-123_x.~");
    CHECK(formEncode("a b") == "a%20b");
    CHECK(formEncode("a&ACTION=DELETE") == "a%26ACTION%3DDELETE");
    CHECK(formEncode("x\r\nHost: evil") == "x%0D%0AHost%3A%20evil");
    CHECK(formEncode("<call:4>W1AW <eor>") == "%3Ccall%3A4%3EW1AW%20%3Ceor%3E");
    CHECK(formEncode("+") == "%2B");
    CHECK(formEncode("\xC3\xA9") == "%C3%A9"); // UTF-8 bytes
    CHECK(formBody({{"KEY", "AB&CD"}, {"ACTION", "INSERT"}}) == "KEY=AB%26CD&ACTION=INSERT");
    CHECK(formDecode(formEncode("x&y=z %/")) == "x&y=z %/");
    CHECK(formDecode("100%") == "100%");

    std::printf("QRZ replies\n");
    {
        const auto r = parseQrz("RESULT=OK&LOGID=130877825&COUNT=1");
        CHECK(r.result == "OK" && r.values.at("LOGID") == "130877825" && r.values.at("COUNT") == "1");
        CHECK(qrzInsertOutcome(r) == Outcome::Done);
    }
    {
        const auto r = parseQrz("RESULT=FAIL&REASON=Unable to add QSO to database: duplicate&EXTENDED=");
        CHECK(r.result == "FAIL" && qrzInsertOutcome(r) == Outcome::Done);
    }
    {
        const auto r = parseQrz("RESULT=FAIL&REASON=invalid api key ABCD-0A0B");
        CHECK(qrzInsertOutcome(r) == Outcome::Stop);
    }
    CHECK(qrzInsertOutcome(parseQrz("RESULT=AUTH&REASON=insufficient privileges")) == Outcome::Stop);
    CHECK(qrzInsertOutcome(parseQrz("<html>Bad gateway</html>")) == Outcome::Retry);
    CHECK(qrzInsertOutcome(parseQrz("")) == Outcome::Retry);
    {
        // STATUS: DATA's pairs, plain or as &amp;
        const auto r = parseQrz("RESULT=OK&DATA=CALLSIGN=KC9OXP&amp;BOOKID=12345&amp;COUNT=1234&amp;OWNER=KC9OXP\n");
        CHECK(r.result == "OK");
        CHECK(r.values.count("CALLSIGN") && r.values.at("CALLSIGN") == "KC9OXP");
        CHECK(r.values.count("COUNT") && r.values.at("COUNT") == "1234");
        CHECK(r.values.count("OWNER") && r.values.at("OWNER") == "KC9OXP");
        const auto r2 = parseQrz("result=ok&data=owner%3DKC9OXP");
        CHECK(r2.result == "OK");
    }

    std::printf("TQSL command line\n");
    {
        const auto a = tqslArgs("Home EN71ar", "s3cret pw", "C:\\Temp\\q.adi");
        CHECK(a.size() == 10);
        CHECK(a[0] == "-d" && a[1] == "-u" && a[2] == "-a" && a[3] == "compliant" && a[4] == "-x");
        CHECK(a[5] == "-l" && a[6] == "Home EN71ar" && a[7] == "-p" && a[8] == "s3cret pw");
        CHECK(a[9] == "C:\\Temp\\q.adi");
        const std::string shown = tqslArgsShown(a);
        std::printf("  shown: %s\n", shown.c_str());
        CHECK(shown.find("s3cret") == std::string::npos && shown.find("********") != std::string::npos);
        CHECK(shown.find("\"Home EN71ar\"") != std::string::npos);
        // no password: no -p at all
        const auto b = tqslArgs("Home", "", "q.adi");
        CHECK(b.size() == 8 && tqslArgsShown(b).find("-p") == std::string::npos);
    }

    std::printf("TQSL status\n");
    CHECK(tqslStatus(0, "") == 0);
    CHECK(tqslStatus(4, "") == 4);
    CHECK(tqslStatus(1, "Signing QSOs\nFinal Status: Success(0)\n") == 0);
    CHECK(tqslStatus(0, "Something\n11\n\n") == 11);
    CHECK(tqslStatus(7, "Unable to open output file\r\n") == 7);
    CHECK(tqslOutcome(0) == Outcome::Done && tqslOutcome(8) == Outcome::Done && tqslOutcome(9) == Outcome::Done);
    CHECK(tqslOutcome(11) == Outcome::Retry && tqslOutcome(3) == Outcome::Retry);
    CHECK(tqslOutcome(4) == Outcome::Stop && tqslOutcome(2) == Outcome::Stop && tqslOutcome(10) == Outcome::Stop);
    CHECK(tqslStatusText(11).find("reach") != std::string::npos);
    CHECK(tqslStatusText(10).find("station location") != std::string::npos);

    std::printf("TQSL station locations\n");
    {
        const std::string xml = "<StationDataFile>\n"
                                "<StationData name=\"Home\">\n<CALL>KC9OXP</CALL>\n<GRIDSQUARE>EN71ar</GRIDSQUARE>\n"
                                "</StationData>\n"
                                "<StationData  name = 'Field &amp; Day'><CALL>KC9OXP/P</CALL></StationData>\n"
                                "<StationData name=\"Home\"><CALL>KC9OXP</CALL></StationData>\n"
                                "<StationData name=\"Caf&#xE9; &quot;QTH&quot;\"></StationData>\n"
                                "<StationData callname=\"x\" name=\"Second\"></StationData>\n"
                                "</StationDataFile>\n";
        const std::vector<std::string> n = tqslStationNames(xml);
        CHECK(n.size() == 4);
        CHECK(n.size() > 0 && n[0] == "Home");
        CHECK(n.size() > 1 && n[1] == "Field & Day");
        CHECK(n.size() > 2 && n[2] == "Caf\xC3\xA9 \"QTH\"");
        CHECK(n.size() > 3 && n[3] == "Second");
        CHECK(tqslStationNames("").empty());
        CHECK(tqslStationNames("<StationDataFile></StationDataFile>").empty());
        CHECK(tqslStationNames("<StationData name=\"cut").empty());
        CHECK(xmlUnescape("a &lt;b&gt; &bogus; &#65;&") == "a <b> &bogus; A&");
    }

    std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
