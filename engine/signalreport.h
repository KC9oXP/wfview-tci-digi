// Signal report for the QSO log comment: SNR of the signal on the tab in use, measured
// from the receive spectrum, and the radio's S-meter (TCI rx_smeter, dBm).
// Qt-free so it can be tested with g++.
//
//  - snrDb(): the strongest bin within +/- halfWidthHz of the signal, against the median
//    of the 300..2700 Hz passband outside that window (the noise floor). It is "signal
//    over noise floor" in the FFT bin bandwidth, not WSJT-X's 2500 Hz-referenced SNR.
//  - sUnits(): IARU HF scale, S9 = -73 dBm, 6 dB per S unit; above S9 as "S9+12".
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace sigrep
{
// NaN when there is too little spectrum to say.
inline double snrDb(const float *db, int n, double hzPerBin, double centerHz, double halfWidthHz)
{
    if (!db || n < 8 || hzPerBin <= 0.0 || centerHz <= 0.0)
        return std::nan("");
    const int lo = std::max(0, int(std::floor((centerHz - halfWidthHz) / hzPerBin)));
    const int hi = std::min(n - 1, int(std::ceil((centerHz + halfWidthHz) / hzPerBin)));
    if (hi < lo)
        return std::nan("");
    float peak = -1e30f;
    for (int i = lo; i <= hi; ++i)
        peak = std::max(peak, db[i]);
    std::vector<float> noise;
    const int nlo = std::max(0, int(300.0 / hzPerBin)), nhi = std::min(n - 1, int(2700.0 / hzPerBin));
    for (int i = nlo; i <= nhi; ++i)
        if (i < lo || i > hi)
            noise.push_back(db[i]);
    if (noise.size() < 8)
        return std::nan("");
    std::nth_element(noise.begin(), noise.begin() + long(noise.size() / 2), noise.end());
    return double(peak) - double(noise[noise.size() / 2]);
}

// "S7", "S9", "S9+12" from dBm (HF scale).
inline std::string sUnits(double dbm)
{
    char buf[24];
    if (dbm > -73.0 + 0.5)
        std::snprintf(buf, sizeof buf, "S9+%d", int(std::lround(dbm + 73.0)));
    else
        std::snprintf(buf, sizeof buf, "S%d", int(std::clamp(std::lround((dbm + 127.0) / 6.0), 0L, 9L)));
    return buf;
}

// "SNR 18 dB, S7" / "SNR 18 dB" / "S7" / "" (pass NaN for what is not known).
inline std::string note(double snr, double dbm)
{
    std::string out;
    if (!std::isnan(snr))
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "SNR %d dB", int(std::lround(snr)));
        out = buf;
    }
    if (!std::isnan(dbm))
    {
        if (!out.empty())
            out += ", ";
        out += sUnits(dbm);
    }
    return out;
}
} // namespace sigrep
