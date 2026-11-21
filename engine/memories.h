// Software memories: the CSV file format and the US licence-class rules.
// Qt-free so it can be tested with g++.
//
// File: one memory per row, first row is the header. Columns (by name, any order;
// unknown columns are kept out of the way and ignored):
//   Name, Tab, Sub-mode, Dial MHz, Sideband, Band, Classes, Comment
// Dial MHz may also be given in Hz (no decimal point, >= 100000).
// An empty Classes cell means "work it out" (classesFor below).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace memories
{
struct Memory
{
    std::string name;
    std::string tab; // tab name as shown in the app: CW, RTTY, PSK, ...
    std::string subMode; // e.g. BPSK31, Olivia 8/500, 45.45/170; empty = leave as is
    std::int64_t dialHz = 0;
    std::string sideband; // USB, LSB, AM or empty (keep the mode)
    std::string band; // e.g. "20 m"; empty = work it out
    std::string classes; // e.g. "General, Advanced, Extra"; empty = work it out
    std::string comment;
};

// ---------------------------------------------------------------------------
// CSV (RFC 4180: commas, "quoted" fields, "" inside quotes, line breaks inside quotes)

inline std::vector<std::vector<std::string>> parseCsv(const std::string &text)
{
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, any = false;
    size_t i = 0;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB
        && (unsigned char)text[2] == 0xBF)
        i = 3; // UTF-8 byte order mark (Excel writes one)
    for (; i < text.size(); ++i)
    {
        const char c = text[i];
        if (quoted)
        {
            if (c == '"')
            {
                if (i + 1 < text.size() && text[i + 1] == '"')
                {
                    field += '"';
                    ++i;
                }
                else
                    quoted = false;
            }
            else
                field += c;
            continue;
        }
        if (c == '"')
        {
            quoted = true;
            any = true;
        }
        else if (c == ',')
        {
            row.push_back(field);
            field.clear();
            any = true;
        }
        else if (c == '\r' || c == '\n')
        {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            if (any || !field.empty() || !row.empty())
            {
                row.push_back(field);
                rows.push_back(row);
            }
            row.clear();
            field.clear();
            any = false;
        }
        else
        {
            field += c;
            any = true;
        }
    }
    if (any || !field.empty() || !row.empty())
    {
        row.push_back(field);
        rows.push_back(row);
    }
    return rows;
}

inline std::string csvField(const std::string &s)
{
    if (s.find_first_of(",\"\r\n") == std::string::npos && (s.empty() || (s.front() != ' ' && s.back() != ' ')))
        return s;
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '"')
            out += '"';
        out += c;
    }
    out += '"';
    return out;
}

inline std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
        --b;
    return s.substr(a, b - a);
}

inline std::string lower(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return s;
}

// ---------------------------------------------------------------------------
// Frequencies

// "14.070" (MHz), "14.0700 MHz", "14070000" (Hz), "14070 kHz", "14,070.000" -> Hz; 0 if not a frequency.
inline std::int64_t parseDial(const std::string &in)
{
    std::string s = lower(trim(in));
    double scale = 0; // 0 = decide from the number
    auto stripSuffix = [&](const char *suf, double sc)
    {
        const std::string t(suf);
        if (s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0)
        {
            s = trim(s.substr(0, s.size() - t.size()));
            scale = sc;
            return true;
        }
        return false;
    };
    if (!stripSuffix("mhz", 1e6) && !stripSuffix("khz", 1e3) && !stripSuffix("hz", 1))
    {
    }
    std::string intPart, fracPart;
    bool dot = false;
    for (char c : s)
    {
        if (c >= '0' && c <= '9')
            (dot ? fracPart : intPart) += c;
        else if (c == '.' && !dot)
            dot = true;
        else if (c == ',' && !dot)
            continue; // thousands separator
        else
            return 0;
    }
    if (intPart.empty() && fracPart.empty())
        return 0;
    if (intPart.size() > 12 || fracPart.size() > 9)
        return 0;
    if (scale == 0)
    {
        // No unit: a decimal point or a small number means MHz, a big whole number means Hz.
        const std::int64_t whole = intPart.empty() ? 0 : std::stoll(intPart);
        scale = (dot || whole < 100000) ? 1e6 : 1;
    }
    // Exact decimal arithmetic: Hz = whole * scale + frac * scale / 10^len.
    const std::int64_t sc = std::int64_t(scale);
    std::int64_t hz = (intPart.empty() ? 0 : std::stoll(intPart)) * sc;
    std::int64_t div = 1;
    for (size_t k = 0; k < fracPart.size(); ++k)
        div *= 10;
    if (!fracPart.empty())
        hz += std::stoll(fracPart) * sc / div; // sub-Hz digits are dropped
    return hz > 0 ? hz : 0;
}

// 14070000 -> "14.070", 14070500 -> "14.0705", 517000 -> "0.517"
inline std::string formatMHz(std::int64_t hz)
{
    if (hz < 0)
        hz = 0;
    const std::int64_t whole = hz / 1000000, frac = hz % 1000000;
    std::string f = std::to_string(frac);
    f = std::string(6 - f.size(), '0') + f;
    while (f.size() > 3 && f.back() == '0')
        f.pop_back();
    return std::to_string(whole) + "." + f;
}

// ---------------------------------------------------------------------------
// Bands and US licence classes (FCC Part 97, ITU Region 2)

inline std::string bandName(std::int64_t hz)
{
    struct B
    {
        std::int64_t lo, hi;
        const char *name;
    };
    static const B bands[] = {
        {1800000, 2000000, "160 m"},
        {3500000, 4000000, "80 m"},
        {5330000, 5410000, "60 m"},
        {7000000, 7300000, "40 m"},
        {10100000, 10150000, "30 m"},
        {14000000, 14350000, "20 m"},
        {18068000, 18168000, "17 m"},
        {21000000, 21450000, "15 m"},
        {24890000, 24990000, "12 m"},
        {28000000, 29700000, "10 m"},
        {50000000, 54000000, "6 m"},
        {144000000, 148000000, "2 m"},
    };
    for (const B &b : bands)
        if (hz >= b.lo && hz <= b.hi)
            return b.name;
    return std::string();
}

// Wavelength in metres from a band name ("160 m", "60m", "70 cm"); 0 if there is none.
// Used to sort the Band column 160 m, 80 m, 60 m ... 2 m instead of as text.
inline double bandMetres(const std::string &band)
{
    size_t i = 0;
    while (i < band.size() && band[i] == ' ')
        ++i;
    double v = 0;
    bool any = false;
    while (i < band.size() && band[i] >= '0' && band[i] <= '9')
    {
        v = v * 10 + (band[i] - '0');
        any = true;
        ++i;
    }
    if (!any)
        return 0;
    while (i < band.size() && band[i] == ' ')
        ++i;
    if (i + 1 < band.size() && (band[i] == 'c' || band[i] == 'C') && (band[i + 1] == 'm' || band[i + 1] == 'M'))
        v /= 100.0;
    return v;
}

// Puts m into list in its place: after the last row of the same tab whose dial is at or
// below m's, else before that tab's first row, else at the end. Used by "Add built-in..."
// so new rows (e.g. 60 m) land between 80 m and 40 m instead of after 6 m.
template <class List> inline void insertInOrder(List &list, const typename List::value_type &m)
{
    long after = -1, first = -1;
    for (long i = 0; i < long(list.size()); ++i)
    {
        if (list[size_t(i)].tab != m.tab)
            continue;
        if (first < 0)
            first = i;
        if (list[size_t(i)].dialHz <= m.dialHz)
            after = i;
    }
    const long at = after >= 0 ? after + 1 : (first >= 0 ? first : long(list.size()));
    list.insert(list.begin() + at, m);
}

// Band column order: longest wavelength first (160 m before 80 m ... 2 m), then by dial;
// rows with no band go last.
inline bool bandLess(const std::string &a, std::int64_t dialA, const std::string &b, std::int64_t dialB)
{
    const double ma = bandMetres(a), mb = bandMetres(b);
    if (ma != mb)
    {
        if (ma == 0 || mb == 0)
            return mb == 0; // no band: last
        return ma > mb;
    }
    return dialA < dialB;
}

// What is on the air, as the rules see it.
enum class Emission
{
    Cw,
    Data, // RTTY and data: RTTY, PSK, Olivia, Hell, AMTOR ...
    Image // SSTV (phone/image sub-bands)
};

// Emission kind for an app tab; false for the receive-only tabs (NAVTEX, WEFAX, WWV Time).
inline bool emissionForTab(const std::string &tab, Emission &e)
{
    const std::string t = lower(trim(tab));
    if (t == "cw")
        e = Emission::Cw;
    else if (t == "sstv")
        e = Emission::Image;
    else if (t == "rtty" || t == "psk" || t == "hell" || t == "olivia" || t == "amtor")
        e = Emission::Data;
    else
        return false;
    return true;
}

enum Class : unsigned
{
    Novice = 1,
    Technician = 2,
    General = 4,
    Advanced = 8,
    Extra = 16,
    Other = 32 // the user's own class: nothing in the US amateur rules (11 m, MARS, other countries ...)
};

namespace detail
{
struct Seg
{
    std::int64_t lo, hi; // Hz, inclusive
};
inline bool inAny(std::int64_t lo, std::int64_t hi, const std::vector<Seg> &segs)
{
    for (const Seg &s : segs)
        if (lo >= s.lo && hi <= s.hi)
            return true;
    return false;
}
constexpr std::int64_t M(double mhz)
{
    return std::int64_t(mhz * 1e6 + 0.5);
}
} // namespace detail

// Where an emission kind is allowed at all (97.305(c); CW is allowed everywhere).
inline bool emissionAllowed(Emission e, std::int64_t lo, std::int64_t hi)
{
    using namespace detail;
    static const std::vector<Seg> data = {
        {M(1.8), M(2.0)},
        {M(3.5), M(3.6)},
        {M(5.3305), M(5.3335)}, // 60 m: see sixtyMetres below
        {M(5.3465), M(5.3495)},
        {M(5.3515), M(5.3665)},
        {M(5.3715), M(5.3745)},
        {M(5.4035), M(5.4065)},
        {M(7.0), M(7.125)},
        {M(10.1), M(10.15)},
        {M(14.0), M(14.15)},
        {M(18.068), M(18.11)},
        {M(21.0), M(21.2)},
        {M(24.89), M(24.93)},
        {M(28.0), M(28.3)},
        {M(50.1), M(54.0)},
        {M(144.1), M(148.0)},
    };
    static const std::vector<Seg> image = {
        {M(1.8), M(2.0)},
        {M(3.6), M(4.0)},
        {M(7.125), M(7.3)},
        {M(14.15), M(14.35)},
        {M(18.11), M(18.168)},
        {M(21.2), M(21.45)},
        {M(24.93), M(24.99)},
        {M(28.3), M(29.7)},
        {M(50.1), M(54.0)},
        {M(144.1), M(148.0)},
    };
    if (e == Emission::Cw)
        return true;
    return inAny(lo, hi, e == Emission::Data ? data : image);
}

// Classes allowed to send emission e occupying lo..hi Hz (97.301, Region 2). Novice and
// Technician on 80, 40 and 15 m: CW only; on 10 m 28.0-28.5 MHz (data to 28.3, phone above).
// 60 m (97.303(h), from 13 Feb 2026): General and up, CW / data / USB phone (no image), 2.8 kHz
// bandwidth, on the channels centred on 5332, 5348, 5373 and 5405 kHz (100 W ERP; USB dial
// 1.5 kHz below the centre) and anywhere in 5351.5-5366.5 kHz (9.15 W ERP). The channel
// spans below run from the USB dial for 3 kHz, like signalSpan().
inline unsigned classesFor(Emission e, std::int64_t lo, std::int64_t hi)
{
    using namespace detail;
    if (lo > hi)
        std::swap(lo, hi);
    if (!emissionAllowed(e, lo, hi))
        return 0;
    static const std::vector<Seg> sixtyMetres = {
        {M(5.3305), M(5.3335)},
        {M(5.3465), M(5.3495)},
        {M(5.3515), M(5.3665)},
        {M(5.3715), M(5.3745)},
        {M(5.4035), M(5.4065)},
    };
    static const std::vector<Seg> extra = {
        {M(1.8), M(2.0)},
        {M(3.5), M(4.0)},
        {M(7.0), M(7.3)},
        {M(10.1), M(10.15)},
        {M(14.0), M(14.35)},
        {M(18.068), M(18.168)},
        {M(21.0), M(21.45)},
        {M(24.89), M(24.99)},
        {M(28.0), M(29.7)},
        {M(50.0), M(54.0)},
        {M(144.0), M(148.0)},
    };
    static const std::vector<Seg> advanced = {
        {M(1.8), M(2.0)},
        {M(3.525), M(3.6)},
        {M(3.7), M(4.0)},
        {M(7.025), M(7.3)},
        {M(10.1), M(10.15)},
        {M(14.025), M(14.15)},
        {M(14.175), M(14.35)},
        {M(18.068), M(18.168)},
        {M(21.025), M(21.2)},
        {M(21.225), M(21.45)},
        {M(24.89), M(24.99)},
        {M(28.0), M(29.7)},
        {M(50.0), M(54.0)},
        {M(144.0), M(148.0)},
    };
    static const std::vector<Seg> general = {
        {M(1.8), M(2.0)},
        {M(3.525), M(3.6)},
        {M(3.8), M(4.0)},
        {M(7.025), M(7.125)},
        {M(7.175), M(7.3)},
        {M(10.1), M(10.15)},
        {M(14.025), M(14.15)},
        {M(14.225), M(14.35)},
        {M(18.068), M(18.168)},
        {M(21.025), M(21.2)},
        {M(21.275), M(21.45)},
        {M(24.89), M(24.99)},
        {M(28.0), M(29.7)},
        {M(50.0), M(54.0)},
        {M(144.0), M(148.0)},
    };
    // Novice and Technician HF: CW only on these ...
    static const std::vector<Seg> ntCwOnly = {
        {M(3.525), M(3.6)},
        {M(7.025), M(7.125)},
        {M(21.025), M(21.2)},
    };
    // ... and 10 m 28.0-28.5 (CW, RTTY/data to 28.3, phone/image above).
    static const std::vector<Seg> nt10 = {{M(28.0), M(28.5)}};
    static const std::vector<Seg> techVhf = {{M(50.0), M(54.0)}, {M(144.0), M(148.0)}};

    unsigned out = 0;
    if (inAny(lo, hi, sixtyMetres))
        out |= General | Advanced | Extra;
    if (inAny(lo, hi, extra))
        out |= Extra;
    if (inAny(lo, hi, advanced))
        out |= Advanced;
    if (inAny(lo, hi, general))
        out |= General;
    const bool nt = (e == Emission::Cw && inAny(lo, hi, ntCwOnly)) || inAny(lo, hi, nt10);
    if (nt)
        out |= Novice | Technician;
    if (inAny(lo, hi, techVhf))
        out |= Technician; // Novices have no 6 m or 2 m privileges
    return out;
}

// RF span of a signal from the dial: about 3 kHz on the sideband's side (the whole
// audio passband, so a memory is only marked for a class if any tone fits).
inline void signalSpan(std::int64_t dialHz, const std::string &sideband, std::int64_t &lo, std::int64_t &hi)
{
    const std::string sb = lower(trim(sideband));
    if (sb == "lsb")
    {
        lo = dialHz - 3000;
        hi = dialHz;
    }
    else if (sb == "am")
    {
        lo = dialHz - 3000;
        hi = dialHz + 3000;
    }
    else
    {
        lo = dialHz;
        hi = dialHz + 3000;
    }
}

inline std::string classNames(unsigned mask)
{
    static const struct
    {
        unsigned bit;
        const char *name;
    } names[] = {{Novice, "Novice"},
                 {Technician, "Technician"},
                 {General, "General"},
                 {Advanced, "Advanced"},
                 {Extra, "Extra"},
                 {Other, "Other"}};
    std::string out;
    for (const auto &n : names)
        if (mask & n.bit)
        {
            if (!out.empty())
                out += ", ";
            out += n.name;
        }
    return out;
}

// "General, Extra", "G E", "tech general", "N T G A E" -> mask (unknown words ignored)
inline unsigned parseClasses(const std::string &text)
{
    unsigned out = 0;
    std::string word;
    auto flush = [&]()
    {
        const std::string w = lower(word);
        word.clear();
        if (w.empty())
            return;
        if (w == "n" || w.rfind("nov", 0) == 0)
            out |= Novice;
        else if (w == "t" || w.rfind("tech", 0) == 0)
            out |= Technician;
        else if (w == "g" || w.rfind("gen", 0) == 0)
            out |= General;
        else if (w == "a" || w.rfind("adv", 0) == 0)
            out |= Advanced;
        else if (w == "e" || w.rfind("ext", 0) == 0)
            out |= Extra;
        else if (w == "other")
            out |= Other;
        else if (w == "all")
            out |= Novice | Technician | General | Advanced | Extra;
    };
    for (char c : text)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            word += c;
        else
            flush();
    }
    flush();
    return out;
}

// Classes text for a memory: its own Classes cell, or worked out from tab + dial.
inline std::string classesText(const Memory &m)
{
    if (!trim(m.classes).empty())
        return trim(m.classes);
    Emission e;
    if (!emissionForTab(m.tab, e))
        return "Receive only";
    std::int64_t lo, hi;
    signalSpan(m.dialHz, m.sideband, lo, hi);
    const unsigned c = classesFor(e, lo, hi);
    return c ? classNames(c) : std::string("Check the rules (outside US sub-bands)");
}

// ---------------------------------------------------------------------------
// Reading and writing a memories file

inline const std::vector<std::string> &columns()
{
    static const std::vector<std::string> c
        = {"Name", "Tab", "Sub-mode", "Dial MHz", "Sideband", "Band", "Classes", "Comment"};
    return c;
}

// Rows with no name or no usable frequency are skipped; skipped counts them.
inline std::vector<Memory> fromCsv(const std::string &text, int *skipped = nullptr)
{
    std::vector<Memory> out;
    int skip = 0;
    const auto rows = parseCsv(text);
    if (rows.empty())
    {
        if (skipped)
            *skipped = 0;
        return out;
    }
    // Column positions from the header (case-insensitive; a few other spellings accepted).
    int col[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    const auto &hdr = rows.front();
    for (int i = 0; i < int(hdr.size()); ++i)
    {
        std::string h = lower(trim(hdr[size_t(i)]));
        std::string k;
        for (char c : h)
            if (c != ' ' && c != '-' && c != '_' && c != '(' && c != ')')
                k += c;
        int which = -1;
        if (k == "name")
            which = 0;
        else if (k == "tab" || k == "mode")
            which = 1;
        else if (k == "submode")
            which = 2;
        else if (k == "dialmhz" || k == "dial" || k == "frequency" || k == "freq" || k == "freqmhz"
                 || k == "frequencymhz" || k == "dialhz" || k == "freqhz")
            which = 3;
        else if (k == "sideband" || k == "radiomode")
            which = 4;
        else if (k == "band")
            which = 5;
        else if (k == "classes" || k == "class" || k == "licenceclasses" || k == "licenseclasses" || k == "licence"
                 || k == "license")
            which = 6;
        else if (k == "comment" || k == "comments" || k == "notes" || k == "note")
            which = 7;
        if (which >= 0 && col[which] < 0)
            col[which] = i;
    }
    if (col[0] < 0 || col[3] < 0)
    {
        if (skipped)
            *skipped = int(rows.size()) - 1;
        return out; // not a memories file
    }
    auto cell = [&](const std::vector<std::string> &r, int which) -> std::string
    { return col[which] >= 0 && col[which] < int(r.size()) ? trim(r[size_t(col[which])]) : std::string(); };
    for (size_t r = 1; r < rows.size(); ++r)
    {
        const auto &row = rows[r];
        bool blank = true;
        for (const auto &f : row)
            if (!trim(f).empty())
                blank = false;
        if (blank)
            continue;
        Memory m;
        m.name = cell(row, 0);
        m.tab = cell(row, 1);
        m.subMode = cell(row, 2);
        m.dialHz = parseDial(cell(row, 3));
        std::string sb = cell(row, 4);
        for (char &c : sb)
            if (c >= 'a' && c <= 'z')
                c = char(c - 'a' + 'A');
        m.sideband = (sb == "USB" || sb == "LSB" || sb == "AM") ? sb : std::string();
        m.band = cell(row, 5);
        m.classes = cell(row, 6);
        m.comment = cell(row, 7);
        if (m.name.empty() || m.dialHz <= 0)
        {
            ++skip;
            continue;
        }
        out.push_back(m);
    }
    if (skipped)
        *skipped = skip;
    return out;
}

inline std::string toCsv(const std::vector<Memory> &list)
{
    std::string out;
    const auto &c = columns();
    for (size_t i = 0; i < c.size(); ++i)
        out += (i ? "," : "") + c[i];
    out += "\r\n";
    for (const Memory &m : list)
    {
        const std::string f[] = {m.name,
                                 m.tab,
                                 m.subMode,
                                 formatMHz(m.dialHz),
                                 m.sideband,
                                 m.band.empty() ? bandName(m.dialHz) : m.band,
                                 m.classes,
                                 m.comment};
        for (size_t i = 0; i < 8; ++i)
            out += (i ? "," : "") + csvField(f[i]);
        out += "\r\n";
    }
    return out;
}
} // namespace memories
