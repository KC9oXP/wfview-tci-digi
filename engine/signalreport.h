// Signal report for the QSO log comment: SNR of the signal on the tab in use, measured
// from the receive spectrum, and the radio's S-meter (TCI rx_smeter, dBm).
// Qt-free so it can be tested with g++.
//
//  - snrDb(): the strongest bin within +/- halfWidthHz of the signal, against the typical
//    strongest bin of same-width windows elsewhere in 300..2700 Hz (so noise alone reads
//    about 0 dB). snrTwoToneDb() for RTTY: the weaker of mark and space. "Signal over
//    noise" in the FFT bin bandwidth, not WSJT-X's 2500 Hz-referenced SNR.
//  - sUnits(): IARU HF scale, S9 = -73 dBm, 6 dB per S unit; above S9 as "S9+12".
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace sigrep
{
// Below this an SNR is not shown or logged: noise alone reads 0 +/- about 3 dB.
constexpr double kMinSnrDb = 3.0;

// Noise reference for a window `width` bins wide: the strongest bin of each same-width
// window across 300..2700 Hz (half-overlapping, skipping [skipLo, skipHi]), and the median
// of those. Comparing a peak with peaks (not with the median bin) makes noise alone read
// about 0 dB instead of 5-9 dB. NaN if there are too few windows.
inline double noiseRef(const float *db, int n, double hzPerBin, int width, int skipLo, int skipHi)
{
    const int nlo = std::max(0, int(300.0 / hzPerBin)), nhi = std::min(n - 1, int(2700.0 / hzPerBin));
    width = std::max(1, width);
    std::vector<float> peaks;
    // Half-overlapping windows; a wide signal (SSTV) leaves too little room for that, so
    // then every start bin is used.
    for (int step : {std::max(1, width / 2), 1})
    {
        peaks.clear();
        for (int s = nlo; s + width - 1 <= nhi; s += step)
        {
            const int e = s + width - 1;
            if (e >= skipLo && s <= skipHi)
                continue; // overlaps the signal
            float p = -1e30f;
            for (int i = s; i <= e; ++i)
                p = std::max(p, db[i]);
            peaks.push_back(p);
        }
        if (peaks.size() >= 4)
            break;
    }
    if (peaks.size() < 4 && width > 8)
        return noiseRef(db, n, hzPerBin, width / 2, skipLo, skipHi); // no room: narrower (reads ~1 dB high)
    if (peaks.size() < 4)
        return std::nan("");
    std::nth_element(peaks.begin(), peaks.begin() + long(peaks.size() / 2), peaks.end());
    return double(peaks[peaks.size() / 2]);
}

inline double peakIn(const float *db, int lo, int hi)
{
    float p = -1e30f;
    for (int i = lo; i <= hi; ++i)
        p = std::max(p, db[i]);
    return double(p);
}

// The strongest bin within +/- halfWidthHz of centerHz over the noise reference above.
// NaN when there is too little spectrum to say.
inline double snrDb(const float *db, int n, double hzPerBin, double centerHz, double halfWidthHz)
{
    if (!db || n < 8 || hzPerBin <= 0.0 || centerHz <= 0.0)
        return std::nan("");
    const int lo = std::max(0, int(std::floor((centerHz - halfWidthHz) / hzPerBin)));
    const int hi = std::min(n - 1, int(std::ceil((centerHz + halfWidthHz) / hzPerBin)));
    if (hi < lo)
        return std::nan("");
    const double ref = noiseRef(db, n, hzPerBin, hi - lo + 1, lo, hi);
    if (std::isnan(ref))
        return std::nan("");
    return peakIn(db, lo, hi) - ref;
}

// Two-tone signal (RTTY mark and space): both tones must be there, so the weaker of the
// two peaks (each within +/- halfWidthHz of its tone) is measured.
inline double snrTwoToneDb(const float *db, int n, double hzPerBin, double toneAHz, double toneBHz, double halfWidthHz)
{
    if (!db || n < 8 || hzPerBin <= 0.0 || toneAHz <= 0.0 || toneBHz <= 0.0)
        return std::nan("");
    auto range = [&](double f, int &lo, int &hi)
    {
        lo = std::max(0, int(std::floor((f - halfWidthHz) / hzPerBin)));
        hi = std::min(n - 1, int(std::ceil((f + halfWidthHz) / hzPerBin)));
    };
    int alo, ahi, blo, bhi;
    range(toneAHz, alo, ahi);
    range(toneBHz, blo, bhi);
    if (ahi < alo || bhi < blo)
        return std::nan("");
    const double ref
        = noiseRef(db, n, hzPerBin, ahi - alo + 1, std::min(alo, blo), std::max(ahi, bhi));
    if (std::isnan(ref))
        return std::nan("");
    return std::min(peakIn(db, alo, ahi), peakIn(db, blo, bhi)) - ref;
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
    if (!std::isnan(snr) && snr >= kMinSnrDb) // below that it is noise, not a report
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
