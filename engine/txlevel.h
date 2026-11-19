// TX level on a dB scale (GitHub issue #2). The engines keep the level as a linear
// audio gain 0..1, as before, so saved settings do not change. The slider works in
// half-dB steps of attenuation: 0 dB = full level (1.0), kMinDb = the lowest setting.
// Qt-free so it can be tested with g++.
#pragma once

#include <algorithm>
#include <cmath>

namespace txlevel
{
constexpr double kMinDb = -40.0; // bottom of the slider
constexpr int kStepsPerDb = 2; // 0.5 dB per slider step
constexpr int kMinSteps = int(kMinDb) * kStepsPerDb; // -80
constexpr int kMaxSteps = 0;

// Linear gain -> dB, clamped to [kMinDb, 0]. 0 or less gives kMinDb.
inline double levelToDb(double level)
{
    if (!(level > 0.0))
        return kMinDb;
    return std::clamp(20.0 * std::log10(level), kMinDb, 0.0);
}

// dB -> linear gain, clamped to [kMinDb, 0] first.
inline double dbToLevel(double db)
{
    return std::pow(10.0, std::clamp(db, kMinDb, 0.0) / 20.0);
}

inline int levelToSteps(double level)
{
    return std::clamp(int(std::lround(levelToDb(level) * kStepsPerDb)), kMinSteps, kMaxSteps);
}

inline double stepsToLevel(int steps)
{
    return dbToLevel(double(std::clamp(steps, kMinSteps, kMaxSteps)) / kStepsPerDb);
}

inline double stepsToDb(int steps)
{
    return double(std::clamp(steps, kMinSteps, kMaxSteps)) / kStepsPerDb;
}
} // namespace txlevel
