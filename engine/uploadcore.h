// Log uploads (QRZ logbook API, LoTW through TQSL): the parts that need to be exactly
// right, Qt-free so they can be tested with g++.
//
//  - formEncode(): application/x-www-form-urlencoded, every byte outside A-Z a-z 0-9 - . _ ~
//    escaped, so an ADIF record (or a key) can never break out of its field (cf. Git
//    CVE-2020-5260, where a newline in a URL sent credentials to another host).
//  - parseQrz(): QRZ answers name=value pairs ("RESULT=OK&LOGID=1&COUNT=1").
//  - TQSL: the command line for one upload, a copy safe to show (password replaced), and
//    what TQSL's status code means for the queue.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace upload
{
// QRZ logbook API: the only address keys are ever sent to.
constexpr const char *kQrzUrl = "https://logbook.qrz.com/api";

inline std::string formEncode(const std::string &s)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'
            || c == '_' || c == '~')
            out += char(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// "KEY=...&ACTION=INSERT&ADIF=..." with every value encoded.
inline std::string formBody(const std::vector<std::pair<std::string, std::string>> &fields)
{
    std::string out;
    for (const auto &f : fields)
    {
        if (!out.empty())
            out += '&';
        out += f.first + "=" + formEncode(f.second);
    }
    return out;
}

inline std::string formDecode(const std::string &s)
{
    auto hv = [](char h) -> int
    {
        if (h >= '0' && h <= '9')
            return h - '0';
        if (h >= 'A' && h <= 'F')
            return h - 'A' + 10;
        if (h >= 'a' && h <= 'f')
            return h - 'a' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '+')
            out += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && hv(s[i + 1]) >= 0 && hv(s[i + 2]) >= 0)
        {
            out += char(hv(s[i + 1]) * 16 + hv(s[i + 2]));
            i += 2;
        }
        else
            out += s[i];
    }
    return out;
}

struct QrzReply
{
    std::string result; // OK, FAIL, AUTH (empty: not a QRZ answer)
    std::string reason;
    std::map<std::string, std::string> values; // every name=value, upper-case names (DATA ones too)
};

// QRZ replies are name=value pairs joined by '&' (DATA's own pairs too, sometimes as &amp;).
inline QrzReply parseQrz(const std::string &text)
{
    QrzReply r;
    std::string t;
    // treat "&amp;" like "&"
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text.compare(i, 5, "&amp;") == 0)
        {
            t += '&';
            i += 4;
        }
        else if (text[i] != '\r' && text[i] != '\n')
            t += text[i];
    }
    size_t start = 0;
    while (start <= t.size())
    {
        size_t end = t.find('&', start);
        if (end == std::string::npos)
            end = t.size();
        const std::string part = t.substr(start, end - start);
        const size_t eq = part.find('=');
        if (eq != std::string::npos && eq > 0)
        {
            std::string name = part.substr(0, eq);
            for (char &c : name)
                if (c >= 'a' && c <= 'z')
                    c = char(c - 'a' + 'A');
            // DATA=CALLSIGN=KC9OXP: the first pair inside DATA comes as DATA's value
            std::string value = part.substr(eq + 1);
            if (name == "DATA")
            {
                const size_t eq2 = value.find('=');
                if (eq2 != std::string::npos && eq2 > 0)
                {
                    std::string inner = value.substr(0, eq2);
                    for (char &c : inner)
                        if (c >= 'a' && c <= 'z')
                            c = char(c - 'a' + 'A');
                    r.values[inner] = formDecode(value.substr(eq2 + 1));
                }
            }
            if (!r.values.count(name))
                r.values[name] = formDecode(value);
        }
        if (end == t.size())
            break;
        start = end + 1;
    }
    auto it = r.values.find("RESULT");
    if (it != r.values.end())
    {
        r.result = it->second;
        for (char &c : r.result)
            if (c >= 'a' && c <= 'z')
                c = char(c - 'a' + 'A');
    }
    it = r.values.find("REASON");
    if (it != r.values.end())
        r.reason = it->second;
    return r;
}

// What to do with a QSO after QRZ answered an INSERT.
enum class Outcome
{
    Done, // uploaded (or already there): drop it from the queue
    Retry, // try again later (network, server trouble)
    Stop // a login problem: keep it queued, stop uploading until the settings change
};

inline Outcome qrzInsertOutcome(const QrzReply &r)
{
    if (r.result == "OK" || r.result == "REPLACE")
        return Outcome::Done;
    std::string reason = r.reason;
    for (char &c : reason)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    if (r.result == "FAIL" && reason.find("duplicate") != std::string::npos)
        return Outcome::Done; // already in the logbook
    if (r.result == "AUTH")
        return Outcome::Stop;
    if (r.result == "FAIL"
        && (reason.find("key") != std::string::npos || reason.find("auth") != std::string::npos
            || reason.find("subscri") != std::string::npos || reason.find("invalid") != std::string::npos))
        return Outcome::Stop;
    if (r.result == "FAIL")
        return Outcome::Stop; // a QSO QRZ refuses would fail every time: stop and show why
    return Outcome::Retry; // no RESULT: not a QRZ answer (proxy page, outage)
}

// ---------------------------------------------------------------------------
// TQSL

// Arguments for signing and uploading one ADIF file. The password is its own argument
// (never through a shell); it is left out when empty (certificate without a password).
inline std::vector<std::string> tqslArgs(const std::string &location,
                                         const std::string &password,
                                         const std::string &adifPath)
{
    std::vector<std::string> a = {"-d", "-u", "-a", "compliant", "-x"};
    if (!location.empty())
    {
        a.push_back("-l");
        a.push_back(location);
    }
    if (!password.empty())
    {
        a.push_back("-p");
        a.push_back(password);
    }
    a.push_back(adifPath);
    return a;
}

// The same command line safe for the TCI log: the password replaced.
inline std::string tqslArgsShown(const std::vector<std::string> &args)
{
    std::string out;
    for (size_t i = 0; i < args.size(); ++i)
    {
        std::string v = args[i];
        if (i > 0 && args[i - 1] == "-p")
            v = "********";
        else if (v.find(' ') != std::string::npos)
            v = "\"" + v + "\"";
        if (!out.empty())
            out += ' ';
        out += v;
    }
    return out;
}

// TQSL's status code: the process exit code, or the number on its last stderr line in -x mode.
inline int tqslStatus(int exitCode, const std::string &stderrText)
{
    // last line that ends in a number ("Final Status: Success(0)" or a bare "0")
    std::string last;
    size_t end = stderrText.size();
    while (end > 0)
    {
        size_t b = stderrText.rfind('\n', end - 1);
        const size_t s = b == std::string::npos ? 0 : b + 1;
        std::string line = stderrText.substr(s, end - s);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty())
        {
            last = line;
            break;
        }
        if (b == std::string::npos)
            break;
        end = b;
    }
    // a number at the end, possibly in brackets
    size_t i = last.size();
    while (i > 0 && (last[i - 1] == ')' || last[i - 1] == ' '))
        --i;
    size_t j = i;
    while (j > 0 && last[j - 1] >= '0' && last[j - 1] <= '9')
        --j;
    if (j < i && i - j <= 2)
        return std::stoi(last.substr(j, i - j));
    return exitCode;
}

inline Outcome tqslOutcome(int status)
{
    switch (status)
    {
    case 0: // signed and uploaded
    case 8: // nothing processed: duplicates or out of date range
    case 9: // some ignored as duplicates
        return Outcome::Done;
    case 3: // unexpected server response
    case 11: // LoTW connection error
        return Outcome::Retry;
    default: // 1 cancelled, 2 rejected, 4 TQSL error (password, certificate), 5 TQSLlib,
             // 6/7 files, 10 syntax
        return Outcome::Stop;
    }
}

inline std::string tqslStatusText(int status)
{
    switch (status)
    {
    case 0:
        return "uploaded";
    case 1:
        return "cancelled";
    case 2:
        return "rejected by LoTW";
    case 3:
        return "unexpected answer from the LoTW server";
    case 4:
        return "TQSL error (password, certificate or station location?)";
    case 5:
        return "TQSL library error (password or certificate?)";
    case 6:
        return "TQSL could not open the log file";
    case 7:
        return "TQSL could not write its output";
    case 8:
        return "already uploaded (duplicate)";
    case 9:
        return "uploaded (some were duplicates)";
    case 10:
        return "TQSL command error";
    case 11:
        return "cannot reach LoTW (no internet?)";
    default:
        return "TQSL status " + std::to_string(status);
    }
}
} // namespace upload
