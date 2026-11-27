#pragma once

// Squelch shared by the decoders that have one (CW, RTTY, PSK, Olivia /
// Contestia, MT63). Qt-free.
//
// Each decoder measures its own level (CW and MT63 a signal-to-noise in dB,
// RTTY tone contrast, PSK phase quality, Olivia the library's block S/N) and,
// for RTTY, PSK and Olivia, a dB figure as well; the user picks which one the
// threshold applies to (the dB box on the tab). What is the same everywhere:
//   - a threshold of 0 (or below) turns the squelch off: everything prints;
//   - it opens once the level has been at or above the threshold for openSec
//     (time above counts down again while below, so a weak signal that dips
//     now and then still opens it);
//   - once open, a level within 'hysteresis' below the threshold keeps it
//     open, and it closes only after hangSec below that; the hang is never
//     longer than it has been open, so a noise spike that just opens it
//     does not hold it open (and print junk) for the whole hang.

#include <algorithm>
#include <cmath>

namespace squelch
{
struct Timing
{
    double openSec = 0.0; // level must be above this long to open
    double hangSec = 0.0; // stays open this long after it falls
    double hysteresis = 0.0; // in the level's own units
};

class Gate
{
public:
    void setThreshold(double t)
    {
        m_threshold = t;
        if (off())
            reset();
    }
    double threshold() const
    {
        return m_threshold;
    }
    bool off() const
    {
        return !(m_threshold > 0.0);
    }
    void setTiming(const Timing &t)
    {
        m_timing = t;
    }
    const Timing &timing() const
    {
        return m_timing;
    }
    void reset()
    {
        m_open = false;
        m_aboveSec = 0.0;
        m_hangLeft = 0.0;
        m_openSec = 0.0;
    }
    // One measurement covering dt seconds. Returns open().
    bool update(double level, double dt)
    {
        m_level = level;
        if (off())
            return true;
        const bool above = level >= m_threshold;
        const bool held = m_open && level >= m_threshold - m_timing.hysteresis;
        if (above)
        {
            m_aboveSec = std::min(m_aboveSec + dt, m_timing.openSec);
            if (m_aboveSec >= m_timing.openSec - 1e-9)
                m_open = true;
        }
        else
            m_aboveSec = std::max(0.0, m_aboveSec - dt);
        if (m_open)
            m_openSec += dt;
        if (above || held)
            m_hangLeft = std::min(m_timing.hangSec, m_openSec);
        else if (m_open)
        {
            m_hangLeft -= dt;
            if (m_hangLeft <= 1e-9)
            {
                m_open = false;
                m_openSec = 0.0;
            }
        }
        return open();
    }
    bool open() const
    {
        return off() || m_open;
    }
    // Last level passed to update().
    double level() const
    {
        return m_level;
    }

private:
    double m_threshold = 0.0;
    Timing m_timing;
    bool m_open = false;
    double m_aboveSec = 0.0;
    double m_hangLeft = 0.0;
    double m_openSec = 0.0; // how long it has been open
    double m_level = 0.0;
};

// Amplitude ratio / power ratio to dB, with a floor so silence does not give
// -inf (the meters start at 0 dB anyway).
inline double ampDb(double ratio)
{
    return 20.0 * std::log10(std::max(ratio, 1e-3));
}
inline double powDb(double ratio)
{
    return 10.0 * std::log10(std::max(ratio, 1e-6));
}
} // namespace squelch
