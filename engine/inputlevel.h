#pragma once
// Receive audio input level for the top-bar dBFS meter (same on every tab). Qt-free.
//
// Meter collects the RX audio wfview sends between two readings (about 300 ms): RMS and peak,
// both in dBFS (0 dBFS = full scale). PeakHold keeps the highest recent peak for the thin
// marker on the bar. zoneFor() picks the colour: is the audio feeding the decoders too quiet,
// about right, too hot or clipping?
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace inlevel
{
constexpr double kFloorDb = -60.0; // left end of the bar
constexpr double kQuietDb = -50.0; // RMS below this: too quiet
constexpr double kHotDb = -12.0;   // RMS above this: too hot
constexpr double kClipDb = -1.0;   // peak at or above this: clipping

inline double toDb(double linear)
{
    return 20.0 * std::log10(std::max(linear, 1e-6)); // floor -120 dBFS
}

struct Meter
{
    double sumSq = 0.0;
    std::size_t count = 0;
    float peak = 0.0f;

    void add(const float *x, std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i)
        {
            const float v = x[i];
            sumSq += double(v) * double(v);
            peak = std::max(peak, std::fabs(v));
        }
        count += n;
    }
    bool empty() const
    {
        return count == 0;
    }
    double rmsDb() const
    {
        return count ? toDb(std::sqrt(sumSq / double(count))) : -120.0;
    }
    double peakDb() const
    {
        return toDb(peak);
    }
    void reset()
    {
        sumSq = 0.0;
        count = 0;
        peak = 0.0f;
    }
};

// Highest peak in the last holdSec seconds, then falls at fallDbPerSec.
struct PeakHold
{
    double holdSec = 1.5;
    double fallDbPerSec = 20.0;
    double db = -120.0;
    double age = 0.0;

    // dtSec: time since the last update; peakDb: peak of that stretch.
    double update(double peakDb, double dtSec)
    {
        age += dtSec;
        if (peakDb >= db)
        {
            db = peakDb;
            age = 0.0;
        }
        else if (age > holdSec)
            db = std::max(peakDb, db - fallDbPerSec * dtSec);
        return db;
    }
    void reset()
    {
        db = -120.0;
        age = 0.0;
    }
};

enum class Zone
{
    Quiet,
    Good,
    Hot,
    Clip
};

inline Zone zoneFor(double rmsDb, double peakDb)
{
    if (peakDb >= kClipDb)
        return Zone::Clip;
    if (rmsDb > kHotDb)
        return Zone::Hot;
    if (rmsDb < kQuietDb)
        return Zone::Quiet;
    return Zone::Good;
}

// Position on the bar, 0 (kFloorDb or below) to 1 (0 dBFS).
inline double fraction(double db)
{
    return std::clamp((db - kFloorDb) / -kFloorDb, 0.0, 1.0);
}
} // namespace inlevel
