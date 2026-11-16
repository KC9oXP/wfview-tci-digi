#pragma once

// Version numbers as used in release tags ("v0.5.3-alpha") and in the app ("0.5.3").
// Qt-free so it can be tested on its own (tests/test_version.cpp).

#include <string>
#include <vector>

namespace versioncheck
{
// The dotted numbers at the start of the text: "v0.5.3-alpha" -> {0, 5, 3}.
// A leading "v" or "V" is skipped; anything after the numbers is ignored.
// Returns an empty list when the text does not start with a number.
inline std::vector<int> parse(const std::string &text)
{
    std::vector<int> out;
    size_t i = 0;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
        ++i;
    if (i < text.size() && (text[i] == 'v' || text[i] == 'V'))
        ++i;
    while (i < text.size() && out.size() < 6)
    {
        if (text[i] < '0' || text[i] > '9')
            break;
        long n = 0;
        int digits = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9' && digits < 9)
        {
            n = n * 10 + (text[i] - '0');
            ++i;
            ++digits;
        }
        out.push_back(int(n));
        if (i < text.size() && text[i] == '.')
            ++i;
        else
            break;
    }
    return out;
}

// -1, 0 or +1 for a older than, same as, newer than b. Missing parts count as 0 ("0.5" == "0.5.0").
inline int compare(const std::vector<int> &a, const std::vector<int> &b)
{
    const size_t n = a.size() > b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i)
    {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

// True when the release tag names a higher version than the running one.
// False when either cannot be read as a version.
inline bool isNewer(const std::string &releaseTag, const std::string &running)
{
    const std::vector<int> r = parse(releaseTag);
    const std::vector<int> c = parse(running);
    if (r.empty() || c.empty())
        return false;
    return compare(r, c) > 0;
}

// "0.5.3" from {0, 5, 3}.
inline std::string toString(const std::vector<int> &v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (i)
            s += '.';
        s += std::to_string(v[i]);
    }
    return s;
}
} // namespace versioncheck
