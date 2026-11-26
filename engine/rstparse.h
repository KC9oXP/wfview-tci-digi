// Finds the signal report the other station sent us in decoded text ("UR RST 599 599",
// "RST 579", "UR 5NN", "ur rst is 559"), for "RST rcvd" in the QSO log. Qt-free.
//
// A report counts only when it follows a report word (UR, YR, YOUR, RST, RPT, REPORT)
// and our own call appears within the 40 words before it (so it was sent to us), and,
// when the other station's call is known, theirs too. The latest one wins.
// CW cut numbers: N = 9, T = 0 ("5NN" = 599).
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace rpt
{

struct Found
{
    std::string report; // "599", "57"; empty: none
    std::string context; // the text from our call to the report, for a tooltip
};

namespace detail
{
struct Word
{
    std::string w;
    size_t begin, end; // in the text
};

inline std::vector<Word> words(const std::string &text)
{
    std::vector<Word> out;
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && !std::isalnum(static_cast<unsigned char>(text[i])))
            ++i;
        const size_t b = i;
        std::string w;
        while (i < text.size() && std::isalnum(static_cast<unsigned char>(text[i])))
            w += char(std::toupper(static_cast<unsigned char>(text[i++])));
        if (!w.empty())
            out.push_back({w, b, i});
    }
    return out;
}

// "KC9OXP/P" -> "KC9OXP"; "VE3/KC9OXP" -> "KC9OXP" (the longest part with a digit).
inline std::string baseCall(const std::string &call)
{
    std::string best, part;
    auto take = [&]()
    {
        const bool digit = std::any_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (digit && part.size() > best.size())
            best = part;
        part.clear();
    };
    for (char c : call)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            part += char(std::toupper(static_cast<unsigned char>(c)));
        else
            take();
    }
    take();
    return best;
}

inline bool isKey(const std::string &w)
{
    return w == "UR" || w == "YR" || w == "YOUR" || w == "RST" || w == "RPT" || w == "REPORT" || w == "RPRT";
}

// Words that can sit between the report word and the number ("UR RST IS 599").
inline bool isFiller(const std::string &w)
{
    return isKey(w) || w == "IS" || w == "SIG" || w == "SIGS" || w == "HR" || w == "ES";
}

// "5NN" -> "599"; "579" stays; anything that is not a valid RST / RS -> "".
inline std::string report(const std::string &w)
{
    if (w.size() != 2 && w.size() != 3)
        return std::string();
    std::string d;
    bool hasDigit = false;
    for (char c : w)
    {
        if (c >= '0' && c <= '9')
        {
            d += c;
            hasDigit = true;
        }
        else if (c == 'N')
            d += '9';
        else if (c == 'T')
            d += '0';
        else
            return std::string();
    }
    if (!hasDigit)
        return std::string();
    if (d[0] < '1' || d[0] > '5')
        return std::string();
    for (size_t k = 1; k < d.size(); ++k)
        if (d[k] < '1' || d[k] > '9')
            return std::string();
    return d;
}
} // namespace detail

inline Found findReport(const std::string &text, const std::string &myCall, const std::string &theirCall)
{
    using namespace detail;
    Found out;
    const std::string me = baseCall(myCall);
    if (me.empty())
        return out;
    const std::string them = baseCall(theirCall);
    const std::vector<Word> ws = words(text);
    for (size_t i = 0; i < ws.size(); ++i)
    {
        if (!isKey(ws[i].w))
            continue;
        size_t j = i + 1;
        while (j < ws.size() && j <= i + 3 && isFiller(ws[j].w))
            ++j;
        if (j >= ws.size() || j > i + 3)
            continue;
        const std::string r = report(ws[j].w);
        if (r.empty())
            continue;
        // Sent to us: our call (and theirs, if known) in the 40 words before.
        const size_t from = i >= 40 ? i - 40 : 0;
        size_t mePos = std::string::npos;
        bool themSeen = them.empty();
        for (size_t k = from; k < i; ++k)
        {
            if (baseCall(ws[k].w) == me)
                mePos = k;
            if (!them.empty() && ws[k].w == them)
                themSeen = true;
        }
        if (mePos == std::string::npos || !themSeen)
            continue;
        out.report = r;
        out.context = text.substr(ws[mePos].begin, ws[j].end - ws[mePos].begin);
        for (char &c : out.context)
            if (c == '\n' || c == '\r')
                c = ' ';
        i = j;
    }
    return out;
}

} // namespace rpt
