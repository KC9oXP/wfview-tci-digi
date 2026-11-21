#pragma once
// STANAG 4481 Easteregg (Options > STANAG 4481 Easteregg, on by default). Qt-free.
//
// NATO STANAG 4481 FSK shore-to-ship broadcasts (most often 75 baud, 850 Hz shift) are
// synchronous and KG-84 encrypted: a continuous stream of random-looking bits with no start or
// stop bits. Our asynchronous RTTY decoder still finds "characters" in it, but the stop bit is
// right only about half the time. Amateur RTTY, even random text, always has its stop bits.
//
// It trips when all of these hold:
//   1. the RTTY settings are 75 baud / 850 Hz (the STANAG 4481 preset, or set by hand),
//   2. the decoder is locked on a real FSK signal (tone quality well above noise; noise alone
//      also fails the stop bit about half the time, but its quality sits near 0.25),
//   3. at least kMinChars characters since the lock, with 30-70 % of them failing the stop bit,
//   4. the lock has held for kMinLockSec seconds.
// Then the RTTY tab prints a line break, the Zero Wing line and another line break, once per
// lock, and goes back to printing the decode. Losing the lock for kRearmSec arms it again.
#include <cmath>
#include <cstddef>

namespace stanagegg
{
constexpr double kBaud = 75.0;
constexpr double kShiftHz = 850.0;
constexpr double kLockQuality = 0.5;   // lock when quality rises above this
constexpr double kUnlockQuality = 0.45; // and keep it while above this
constexpr double kDropoutSec = 1.0;     // dips shorter than this do not break the lock
constexpr double kMinLockSec = 10.0;
constexpr std::size_t kMinChars = 10;
constexpr double kMinFailRatio = 0.30;
constexpr double kMaxFailRatio = 0.70;
constexpr double kRearmSec = 3.0;

inline const char *message()
{
    return "ALL YOUR BASE ARE BELONG TO US";
}

inline bool settingsMatch(double baud, double shiftHz)
{
    return std::fabs(baud - kBaud) < 0.5 && std::fabs(shiftHz - kShiftHz) < 15.0;
}

class Detector
{
public:
    // Call on every decoder stats update. chars / framingErrors are the decoder's running
    // totals; dtSec is the time since the last call. Returns true once when it trips.
    bool update(bool enabled, double baud, double shiftHz, double quality, std::size_t chars,
                std::size_t framingErrors, double dtSec)
    {
        if (!enabled || !settingsMatch(baud, shiftHz))
        {
            unlock();
            m_fired = false;
            return false;
        }
        const bool good = quality > (m_locked ? kUnlockQuality : kLockQuality);
        if (!m_locked)
        {
            m_lostSec += dtSec;
            if (m_lostSec >= kRearmSec)
                m_fired = false;
            if (good)
            {
                m_locked = true;
                m_lockSec = 0.0;
                m_dipSec = 0.0;
                m_chars0 = chars;
                m_fe0 = framingErrors;
            }
            return false;
        }
        if (good)
            m_dipSec = 0.0;
        else
        {
            m_dipSec += dtSec;
            if (m_dipSec >= kDropoutSec)
            {
                unlock();
                return false;
            }
        }
        m_lockSec += dtSec;
        if (m_fired || m_lockSec < kMinLockSec)
            return false;
        // a decoder reset (new settings) sets its totals back to 0
        if (chars < m_chars0 || framingErrors < m_fe0)
        {
            m_chars0 = chars;
            m_fe0 = framingErrors;
            return false;
        }
        const std::size_t ok = chars - m_chars0, bad = framingErrors - m_fe0;
        const std::size_t n = ok + bad;
        if (n < kMinChars)
            return false;
        const double r = double(bad) / double(n);
        if (r < kMinFailRatio || r > kMaxFailRatio)
            return false;
        m_fired = true;
        return true;
    }

    bool locked() const
    {
        return m_locked;
    }

private:
    void unlock()
    {
        if (m_locked)
            m_lostSec = 0.0;
        m_locked = false;
        m_lockSec = 0.0;
        m_dipSec = 0.0;
    }

    bool m_locked = false;
    bool m_fired = false;
    double m_lockSec = 0.0;
    double m_dipSec = 0.0;
    double m_lostSec = 0.0;
    std::size_t m_chars0 = 0, m_fe0 = 0;
};
} // namespace stanagegg
