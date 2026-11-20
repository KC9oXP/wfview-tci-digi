// Auto TX level: find the highest TX level whose ALC reading is at or below a target,
// from short ALC measurements of a steady tone. Qt-free so it can be tested with g++.
//
// The search works in dB on the TX level scale (engine/txlevel.h, 0 dB = full level):
//   1. Step up 3 dB at a time while the ALC is at or below the target, until it goes
//      over (or full level is reached).
//   2. Step down 3 dB at a time while the ALC is over the target, until it is at or
//      below (or the bottom of the scale is reached).
//   3. Halve the gap between the highest level known to be at/below the target and
//      the lowest known to be over it, until the gap is 0.5 dB (one slider step).
// The answer is the highest level at or below the target. With a target of 0 that
// means: first find a level that shows ALC, then come down until it reads 0.
#pragma once

#include "txlevel.h"

#include <cmath>

namespace autolevel
{
constexpr double kStepDb = 3.0; // search step before the bracket is found
constexpr double kResolutionDb = 0.5; // one TX level slider step
constexpr double kZeroAlc = 0.005; // readings at or below this count as no ALC

class Search
{
public:
    enum class State
    {
        Measuring, // set the level to currentDb() and feed() the next ALC reading
        Done, // resultDb() is the answer
        AtMaxBelow, // full level and the ALC is still at or below the target
        AtMinOver // the bottom of the scale and the ALC is still over the target
    };

    Search(double startDb, double targetAlc)
        : m_target(targetAlc < 0.0 ? 0.0 : targetAlc)
    {
        m_cur = snap(startDb <= txlevel::kMinDb ? -20.0 : startDb);
    }

    double target() const
    {
        return m_target;
    }
    double currentDb() const
    {
        return m_cur;
    }
    State state() const
    {
        return m_state;
    }
    int steps() const
    {
        return m_steps;
    }
    // Highest level at or below the target (the answer when Done or AtMaxBelow).
    double resultDb() const
    {
        return m_lo;
    }
    // ALC that was read at resultDb().
    double resultAlc() const
    {
        return m_loAlc;
    }
    bool okAt(double alc) const
    {
        return m_target <= 0.0 ? alc <= kZeroAlc : alc <= m_target + 1e-9;
    }

    // The ALC measured at currentDb(); moves on to the next level to try.
    State feed(double alc)
    {
        if (m_state != State::Measuring)
            return m_state;
        ++m_steps;
        if (okAt(alc))
        {
            m_lo = m_cur;
            m_loAlc = alc;
            m_haveLo = true;
        }
        else
        {
            m_hi = m_cur;
            m_haveHi = true;
        }

        if (!m_haveHi)
        {
            if (m_cur >= 0.0)
                return m_state = State::AtMaxBelow;
            m_cur = snap(std::fmin(0.0, m_cur + kStepDb));
            return m_state;
        }
        if (!m_haveLo)
        {
            if (m_cur <= txlevel::kMinDb)
                return m_state = State::AtMinOver;
            m_cur = snap(std::fmax(txlevel::kMinDb, m_cur - kStepDb));
            return m_state;
        }
        if (m_hi - m_lo <= kResolutionDb + 1e-9)
            return m_state = State::Done;
        m_cur = snap((m_lo + m_hi) / 2.0);
        if (m_cur <= m_lo || m_cur >= m_hi) // cannot split further on the 0.5 dB grid
            return m_state = State::Done;
        return m_state;
    }

private:
    static double snap(double db)
    {
        const double s = std::round(db / kResolutionDb) * kResolutionDb;
        return s < txlevel::kMinDb ? txlevel::kMinDb : (s > 0.0 ? 0.0 : s);
    }

    double m_target = 0.1;
    double m_cur = -20.0;
    double m_lo = txlevel::kMinDb, m_hi = 0.0;
    double m_loAlc = 0.0;
    bool m_haveLo = false, m_haveHi = false;
    int m_steps = 0;
    State m_state = State::Measuring;
};
} // namespace autolevel
