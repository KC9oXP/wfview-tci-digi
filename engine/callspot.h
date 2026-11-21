// Callsigns in decoded text, for PSK Reporter spots and click-to-log. Qt-free so it can
// be tested with g++.
//
// isCallsign(): amateur callsign shape (prefix, a digit, a suffix ending in a letter),
// with optional /P, /QRP, VE3/ ... parts.
// Spotter: counts "sightings" of a callsign right after DE, CQ or 73 in the decoded text
// of each source (tab) and reports a call once it has been seen `needed` times (default
// once: one CQ / DE / 73 over is enough) within `windowSec` near the same frequency, and
// then not again for `windowSec`. "CQ CQ DE W1AW W1AW W1AW K" is three sightings.
// After 73 the call counts only when DE does not follow it: "73 W1AW SK" is W1AW signing,
// "TU 73 K1ABC DE W1AW" is K1ABC being thanked (not heard), so only W1AW counts.
// A call sent twice in a row with no keyword before it ("VVV TEST TEST N9KRK N9KRK") counts
// too (via "2x"), unless DE follows: "N9KRK N9KRK DE KC9OXP" is KC9OXP calling N9KRK.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace callspot
{
inline std::string upper(std::string s)
{
    for (char &c : s)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    return s;
}

inline bool isAlpha(char c)
{
    return c >= 'A' && c <= 'Z';
}
inline bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

// The part of a callsign without /P etc.: [digit]? letter{1,2} | letter digit, then a
// digit, then 0-3 letters/digits ending in a letter. W1AW, KC9OXP, 2E0ABC, 9A1A, 3DA0XY, E73Y.
inline bool isBaseCall(const std::string &s)
{
    const int n = int(s.size());
    if (n < 3 || n > 8)
        return false;
    for (char c : s)
        if (!isAlpha(c) && !isDigit(c))
            return false;
    if (!isAlpha(s[size_t(n - 1)]))
        return false;
    // Try each prefix shape; the separating digit follows it.
    auto tryPrefix = [&](int plen) -> bool
    {
        if (plen >= n - 1)
            return false;
        if (!isDigit(s[size_t(plen)]))
            return false;
        const int suffix = n - plen - 1;
        return suffix >= 1 && suffix <= 4;
    };
    // letter{1,2}
    if (isAlpha(s[0]) && tryPrefix(1))
        return true;
    if (n > 1 && isAlpha(s[0]) && isAlpha(s[1]) && tryPrefix(2))
        return true;
    // letter digit (E7, T7 ...)
    if (n > 1 && isAlpha(s[0]) && isDigit(s[1]) && tryPrefix(2))
        return true;
    // digit letter{1,2} (2E, 9A, 3DA ...)
    if (isDigit(s[0]) && n > 1 && isAlpha(s[1]))
    {
        if (tryPrefix(2))
            return true;
        if (n > 2 && isAlpha(s[2]) && tryPrefix(3))
            return true;
    }
    return false;
}

inline bool isCallsign(const std::string &in)
{
    const std::string s = upper(in);
    if (s.empty() || s.size() > 16 || s.front() == '/' || s.back() == '/')
        return false;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s)
    {
        if (c == '/')
        {
            parts.push_back(cur);
            cur.clear();
        }
        else if (isAlpha(c) || isDigit(c))
            cur += c;
        else
            return false;
    }
    parts.push_back(cur);
    if (parts.size() > 3)
        return false;
    int bases = 0;
    for (const std::string &p : parts)
    {
        if (p.empty() || p.size() > 8)
            return false;
        if (isBaseCall(p))
            ++bases;
        else if (p.size() > 4)
            return false; // /P, /M, /QRP, /MM, VE3/, /7 ...
    }
    return bases >= 1;
}

// Splits text into words: letters, digits and '/', anything else separates.
inline std::vector<std::string> words(const std::string &text)
{
    std::vector<std::string> out;
    std::string w;
    for (char c0 : text)
    {
        const char c = (c0 >= 'a' && c0 <= 'z') ? char(c0 - 'a' + 'A') : c0;
        if (isAlpha(c) || isDigit(c) || c == '/')
            w += c;
        else if (!w.empty())
        {
            out.push_back(w);
            w.clear();
        }
    }
    if (!w.empty())
        out.push_back(w);
    return out;
}

struct Spot
{
    std::string source; // tab
    std::string call;
    std::int64_t hz = 0; // RF frequency when it was seen
    std::int64_t timeSec = 0;
    std::string via; // "CQ", "DE" or "73": the word before the call
    std::string to; // "K1ABC DE W1AW": K1ABC (empty if not known)
};

class Spotter
{
public:
    int needed = 1; // sightings before a call is reported
    int windowSec = 600; // ... within this long
    std::int64_t maxSpreadHz = 1500; // ... and this close in frequency
    std::string myCall; // never reported

    // Decoded text as it arrives (any size of piece). hz: RF frequency of the signal now.
    // Returns the calls that reached `needed` sightings with this text.
    std::vector<Spot> feed(const std::string &source, const std::string &text, std::int64_t hz, std::int64_t nowSec)
    {
        std::vector<Spot> out;
        Stream &st = m_streams[source];
        // Only whole words: the last piece may be the start of a word still arriving.
        std::string all = st.partial + text;
        size_t cut = all.size();
        while (cut > 0)
        {
            const char c = all[cut - 1];
            const char u = (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
            if (!(isAlpha(u) || isDigit(u) || u == '/'))
                break;
            --cut;
        }
        st.partial = all.substr(cut);
        if (st.partial.size() > 32)
            st.partial.clear(); // a run of noise with no spaces
        for (const std::string &w : words(all.substr(0, cut)))
            word(source, st, w, hz, nowSec, out);
        return out;
    }

    // Number of sightings of call (for tests and the status line).
    int sightings(const std::string &call, std::int64_t nowSec) const
    {
        auto it = m_seen.find(upper(call));
        if (it == m_seen.end())
            return 0;
        int n = 0;
        for (const Seen &s : it->second.list)
            if (nowSec - s.t <= windowSec)
                ++n;
        return n;
    }

    void clear()
    {
        m_streams.clear();
        m_seen.clear();
    }

private:
    struct Stream
    {
        std::string partial;
        int after = 0; // 1 = last keyword DE, 2 = CQ, 3 = 73
        std::string lastCall; // for "DE W1AW W1AW"
        int skips = 0; // words skipped after CQ or 73 (CQ DX, CQ TEST, TU 73 GL ...)
        std::string pending; // call after 73, counted once the next word is not DE
        std::string prev; // the word before this one
        std::string to; // the call before DE ("K1ABC DE W1AW")
        bool cqDe = false; // "CQ CQ DE W1AW": a CQ, though DE came last
        std::string dbl; // "W1AW W1AW" with no keyword: counted once the next word is not DE
    };
    struct Seen
    {
        std::int64_t t, hz;
    };
    struct Track
    {
        std::vector<Seen> list;
        std::int64_t reportedAt = -1;
    };

    void word(const std::string &source,
              Stream &st,
              const std::string &w,
              std::int64_t hz,
              std::int64_t now,
              std::vector<Spot> &out)
    {
        const std::string before = st.prev;
        st.prev = w;
        wordIn(source, st, before, w, hz, now, out);
    }

    void wordIn(const std::string &source,
              Stream &st,
              const std::string &before,
              const std::string &w,
              std::int64_t hz,
              std::int64_t now,
              std::vector<Spot> &out)
    {
        if (!st.pending.empty())
        {
            // "73 W1AW SK": W1AW signed. "73 K1ABC DE W1AW": K1ABC was only addressed.
            if (w != "DE" && w != st.pending)
                sighting(source, st.pending, "73", std::string(), hz, now, out);
            if (w != st.pending)
                st.pending.clear();
            else
                return; // "73 W1AW W1AW": wait for the word after the repeats
        }
        if (!st.dbl.empty())
        {
            // "N9KRK N9KRK TE" / "N9KRK N9KRK K": N9KRK sent it. "N9KRK N9KRK DE KC9OXP": called.
            if (w == st.dbl)
                return; // more repeats
            if (w != "DE")
                sighting(source, st.dbl, "2x", std::string(), hz, now, out);
            st.dbl.clear();
        }
        if (w == "DE")
        {
            st.cqDe = st.after == 2 && st.lastCall.empty();
            st.after = 1;
            st.lastCall.clear();
            st.to = isCallsign(before) ? before : std::string();
            return;
        }
        if (w == "CQ" || w == "73")
        {
            st.after = (w == "CQ") ? 2 : 3;
            st.skips = 0;
            st.lastCall.clear();
            st.to.clear();
            return;
        }
        if (st.after == 0)
        {
            if (w == before && isCallsign(w))
                st.dbl = w; // "W1AW W1AW": wait for the next word
            return;
        }
        if (isCallsign(w))
        {
            if (st.after == 3)
            {
                st.pending = w;
                st.after = 0;
                return;
            }
            if (st.lastCall.empty() || st.lastCall == w)
            {
                st.lastCall = w;
                sighting(source, w, (st.after == 2 || st.cqDe) ? "CQ" : "DE", st.to, hz, now, out);
                return; // "DE W1AW W1AW": stay after the keyword for repeats
            }
            st.after = 0; // a different call: "DE W1AW K1ABC" is not two sightings of either
            return;
        }
        if (st.after >= 2 && st.lastCall.empty() && ++st.skips <= 2 && w.size() <= 5)
            return; // "CQ DX W1AW", "CQ TEST W1AW", "CQ POTA W1AW", "73 GL W1AW"
        st.after = 0;
    }

    void sighting(const std::string &source,
                  const std::string &call,
                  const std::string &via,
                  const std::string &to,
                  std::int64_t hz,
                  std::int64_t now,
                  std::vector<Spot> &out)
    {
        if (!myCall.empty() && upper(myCall) == call)
            return;
        Track &t = m_seen[call];
        // keep recent sightings near this frequency
        std::vector<Seen> keep;
        for (const Seen &s : t.list)
            if (now - s.t <= windowSec && std::llabs(s.hz - hz) <= maxSpreadHz)
                keep.push_back(s);
        keep.push_back({now, hz});
        t.list = keep;
        if (int(t.list.size()) >= needed && (t.reportedAt < 0 || now - t.reportedAt > windowSec))
        {
            t.reportedAt = now;
            out.push_back({source, call, hz, now, via, to == call ? std::string() : to});
            t.list.clear();
        }
    }

    std::map<std::string, Stream> m_streams;
    std::map<std::string, Track> m_seen;
};
} // namespace callspot
