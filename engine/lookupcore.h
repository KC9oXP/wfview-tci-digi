// Callsign lookup for the QSO log (Name / QTH / Grid): the parts that need to be exactly
// right, Qt-free so they can be tested with g++.
//
// Two callbooks:
//  - QRZ.com XML service: https://xmldata.qrz.com/xml/current/ ; login with the QRZ
//    username and password (not the logbook API key) gives a session <Key>; lookups pass
//    s=<key>;callsign=<call>. Without an XML Logbook Data subscription QRZ answers with
//    name and address only (no grid) plus a <Message> saying so.
//  - HamQTH (free, account needed): https://www.hamqth.com/xml.php ; u= p= gives a
//    <session_id> (valid about an hour); lookups pass id=, callsign=, prg=.
// Both answers are small flat XML documents: tagText() pulls the first <tag>text</tag>
// and decodes the XML entities. Nothing here talks to the network.
#pragma once

#include "uploadcore.h" // formEncode

#include <cstdlib>
#include <string>

namespace lookup
{
constexpr const char *kQrzXmlUrl = "https://xmldata.qrz.com/xml/current/";
constexpr const char *kHamqthUrl = "https://www.hamqth.com/xml.php";

inline std::string xmlDecode(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '&')
        {
            out += s[i];
            continue;
        }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10)
        {
            out += s[i];
            continue;
        }
        const std::string e = s.substr(i + 1, semi - i - 1);
        if (e == "amp")
            out += '&';
        else if (e == "lt")
            out += '<';
        else if (e == "gt")
            out += '>';
        else if (e == "quot")
            out += '"';
        else if (e == "apos")
            out += '\'';
        else if (e.size() > 1 && e[0] == '#')
        {
            const long cp = e[1] == 'x' || e[1] == 'X' ? std::strtol(e.c_str() + 2, nullptr, 16)
                                                       : std::strtol(e.c_str() + 1, nullptr, 10);
            if (cp > 0 && cp < 0x80)
                out += char(cp);
            else if (cp >= 0x80 && cp < 0x800)
            {
                out += char(0xC0 | (cp >> 6));
                out += char(0x80 | (cp & 0x3F));
            }
            else if (cp >= 0x800 && cp < 0x10000)
            {
                out += char(0xE0 | (cp >> 12));
                out += char(0x80 | ((cp >> 6) & 0x3F));
                out += char(0x80 | (cp & 0x3F));
            }
        }
        else
        {
            out += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

// Text of the first <tag> or <tag attr=...> element; "" if missing. With a namespace prefix
// in the document (<h:nick>) the plain name still matches.
inline std::string tagText(const std::string &xml, const std::string &tag, size_t from = 0)
{
    size_t pos = from;
    while ((pos = xml.find('<', pos)) != std::string::npos)
    {
        size_t nameStart = pos + 1;
        size_t nameEnd = xml.find_first_of(" \t\r\n/>", nameStart);
        if (nameEnd == std::string::npos)
            return {};
        std::string name = xml.substr(nameStart, nameEnd - nameStart);
        const size_t colon = name.find(':');
        if (colon != std::string::npos)
            name = name.substr(colon + 1);
        const size_t gt = xml.find('>', nameEnd);
        if (gt == std::string::npos)
            return {};
        if (name == tag)
        {
            if (xml[gt - 1] == '/')
                return {}; // <tag/>
            const size_t close = xml.find("</", gt + 1);
            if (close == std::string::npos)
                return {};
            std::string t = xmlDecode(xml.substr(gt + 1, close - gt - 1));
            const size_t a = t.find_first_not_of(" \t\r\n");
            const size_t b = t.find_last_not_of(" \t\r\n");
            return a == std::string::npos ? std::string() : t.substr(a, b - a + 1);
        }
        pos = gt + 1;
    }
    return {};
}

// Text of <tag> inside the first <section> ... </section> block (QRZ's <Session> vs <Callsign>).
inline std::string sectionTag(const std::string &xml, const std::string &section, const std::string &tag)
{
    const size_t a = xml.find("<" + section);
    if (a == std::string::npos)
        return {};
    size_t b = xml.find("</" + section, a);
    if (b == std::string::npos)
        b = xml.size();
    return tagText(xml.substr(a, b - a), tag);
}

inline std::string upper(std::string s)
{
    for (char &c : s)
        if (c >= 'a' && c <= 'z')
            c = char(c - 32);
    return s;
}

// Title case for names in capitals ("HERMAN C" -> "Herman C"); mixed case left alone.
inline std::string niceName(const std::string &s)
{
    bool hasLower = false;
    for (char c : s)
        hasLower = hasLower || (c >= 'a' && c <= 'z');
    if (hasLower)
        return s;
    std::string out = s;
    bool start = true;
    for (char &c : out)
    {
        if (c >= 'A' && c <= 'Z' && !start)
            c = char(c + 32);
        start = !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\'');
    }
    return out;
}

struct Result
{
    bool found = false;
    bool sessionBad = false; // log in again and retry once
    bool loginFailed = false; // wrong username / password: stop until settings change
    std::string session; // key / session id from this answer, if any
    std::string call, name, qth, grid, country;
    std::string error, message; // shown to the user
};

// ---- QRZ ------------------------------------------------------------------

inline std::string qrzLoginQuery(const std::string &user, const std::string &pass, const std::string &agent)
{
    return "username=" + upload::formEncode(user) + "&password=" + upload::formEncode(pass)
        + "&agent=" + upload::formEncode(agent);
}

inline std::string qrzLookupQuery(const std::string &key, const std::string &call)
{
    return "s=" + upload::formEncode(key) + "&callsign=" + upload::formEncode(call);
}

inline Result parseQrz(const std::string &xml)
{
    Result r;
    r.session = sectionTag(xml, "Session", "Key");
    r.error = sectionTag(xml, "Session", "Error");
    r.message = sectionTag(xml, "Session", "Message");
    const std::string lerr = upper(r.error);
    if (!r.error.empty())
    {
        if (lerr.find("PASSWORD") != std::string::npos || lerr.find("USERNAME") != std::string::npos)
            r.loginFailed = true;
        else if (lerr.find("SESSION") != std::string::npos || lerr.find("INVALID KEY") != std::string::npos)
            r.sessionBad = true;
    }
    if (xml.find("<Callsign") == std::string::npos)
        return r;
    r.call = upper(sectionTag(xml, "Callsign", "call"));
    r.found = !r.call.empty();
    std::string first = sectionTag(xml, "Callsign", "nickname");
    if (first.empty())
        first = sectionTag(xml, "Callsign", "fname");
    r.name = niceName(first);
    const std::string city = niceName(sectionTag(xml, "Callsign", "addr2"));
    const std::string state = sectionTag(xml, "Callsign", "state");
    r.country = sectionTag(xml, "Callsign", "country");
    r.qth = city;
    if (!state.empty())
        r.qth += (r.qth.empty() ? "" : ", ") + state;
    else if (!r.country.empty() && r.country != "United States")
        r.qth += (r.qth.empty() ? "" : ", ") + r.country;
    r.grid = sectionTag(xml, "Callsign", "grid");
    return r;
}

// ---- HamQTH ---------------------------------------------------------------

inline std::string hamqthLoginQuery(const std::string &user, const std::string &pass)
{
    return "u=" + upload::formEncode(user) + "&p=" + upload::formEncode(pass);
}

inline std::string hamqthLookupQuery(const std::string &id, const std::string &call, const std::string &prg)
{
    return "id=" + upload::formEncode(id) + "&callsign=" + upload::formEncode(call) + "&prg=" + upload::formEncode(prg);
}

inline Result parseHamqth(const std::string &xml)
{
    Result r;
    r.session = tagText(xml, "session_id");
    r.error = tagText(xml, "error");
    const std::string lerr = upper(r.error);
    if (!r.error.empty())
    {
        if (lerr.find("WRONG USER") != std::string::npos || lerr.find("PASSWORD") != std::string::npos)
            r.loginFailed = true;
        else if (lerr.find("SESSION") != std::string::npos)
            r.sessionBad = true;
    }
    if (xml.find("<search") == std::string::npos)
        return r;
    r.call = upper(tagText(xml, "callsign"));
    r.found = !r.call.empty();
    std::string first = tagText(xml, "nick");
    if (first.empty())
        first = tagText(xml, "adr_name");
    r.name = niceName(first);
    std::string city = tagText(xml, "qth");
    if (city.empty())
        city = tagText(xml, "adr_city");
    const std::string state = tagText(xml, "us_state");
    r.country = tagText(xml, "country");
    r.qth = niceName(city);
    if (!state.empty())
        r.qth += (r.qth.empty() ? "" : ", ") + state;
    else if (!r.country.empty() && r.country != "United States")
        r.qth += (r.qth.empty() ? "" : ", ") + r.country;
    r.grid = tagText(xml, "grid");
    return r;
}

// The call to look up: "W1AW/P" -> "W1AW", "VE3/W1AW" -> "W1AW" (the home call holds the
// profile), "EA8/G4ABC/P" -> "G4ABC".
inline std::string baseCall(const std::string &call)
{
    std::string best;
    size_t start = 0;
    while (start <= call.size())
    {
        size_t slash = call.find('/', start);
        if (slash == std::string::npos)
            slash = call.size();
        const std::string part = call.substr(start, slash - start);
        if (part.size() > best.size())
            best = part;
        start = slash + 1;
    }
    return upper(best);
}
} // namespace lookup
