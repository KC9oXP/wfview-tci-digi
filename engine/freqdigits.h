// Digit tuning for the frequency box: which digit of "14.074.000" a character is and
// what stepping it does. Qt-free so it can be tested with g++.
#pragma once

#include <cstdint>
#include <string>

namespace freqdigits
{
constexpr std::int64_t kMaxHz = 999999999; // 9 digits, as the box is sized

// Place value (1, 10, 100...) of the digit at index i of text, or 0 if it is not a digit.
inline std::int64_t placeAt(const std::string &text, int i)
{
    if (i < 0 || i >= int(text.size()) || text[size_t(i)] < '0' || text[size_t(i)] > '9')
        return 0;
    std::int64_t place = 1;
    for (size_t j = size_t(i) + 1; j < text.size(); ++j)
        if (text[j] >= '0' && text[j] <= '9')
            place *= 10;
    return place;
}

// The box text: always 9 digits with leading zeros, so every digit up to 100 MHz can be
// stepped (on 1.810 MHz the 10 MHz digit is a 0 that can be turned up). 1810000 -> "001.810.000"
inline std::string format(std::int64_t hz)
{
    if (hz < 0)
        hz = 0;
    if (hz > kMaxHz)
        hz = kMaxHz;
    std::string d = std::to_string(hz);
    d = std::string(9 - d.size(), '0') + d;
    return d.substr(0, 3) + "." + d.substr(3, 3) + "." + d.substr(6, 3);
}

// Characters of text that are leading zeros (drawn dimmer): "001.810.000" -> 2, "014.074.000" -> 1
inline int leadingZeros(const std::string &text)
{
    int n = 0;
    for (char c : text)
    {
        if (c == '0')
            ++n;
        else if (c == '.')
            continue;
        else
            break;
    }
    // index of the first non-zero digit (dots before it count as dim too)
    int idx = 0, zeros = 0;
    for (; idx < int(text.size()) && zeros < n; ++idx)
        if (text[size_t(idx)] == '0')
            ++zeros;
    while (idx < int(text.size()) && text[size_t(idx)] == '.')
        ++idx;
    return n == 0 ? 0 : idx;
}

// hz moved by steps of place, kept within 1 Hz .. kMaxHz.
inline std::int64_t step(std::int64_t hz, std::int64_t place, int steps)
{
    std::int64_t v = hz + place * steps;
    if (v < 1)
        v = 1;
    if (v > kMaxHz)
        v = kMaxHz;
    return v;
}
} // namespace freqdigits
