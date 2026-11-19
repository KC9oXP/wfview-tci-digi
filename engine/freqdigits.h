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
