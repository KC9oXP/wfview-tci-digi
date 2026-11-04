#include "cwtxcore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
struct Code
{
    char c;
    const char *m;
};
const Code kCodes[] = {
    {'A', ".-"},     {'B', "-..."},   {'C', "-.-."},   {'D', "-.."},     {'E', "."},      {'F', "..-."},
    {'G', "--."},    {'H', "...."},   {'I', ".."},     {'J', ".---"},    {'K', "-.-"},    {'L', ".-.."},
    {'M', "--"},     {'N', "-."},     {'O', "---"},    {'P', ".--."},    {'Q', "--.-"},   {'R', ".-."},
    {'S', "..."},    {'T', "-"},      {'U', "..-"},    {'V', "...-"},    {'W', ".--"},    {'X', "-..-"},
    {'Y', "-.--"},   {'Z', "--.."},   {'0', "-----"},  {'1', ".----"},   {'2', "..---"},  {'3', "...--"},
    {'4', "....-"},  {'5', "....."},  {'6', "-...."},  {'7', "--..."},   {'8', "---.."},  {'9', "----."},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."},   {'=', "-...-"},  {'+', ".-.-."},
    {'-', "-....-"}, {'(', "-.--."},  {')', "-.--.-"}, {'\'', ".----."}, {'"', ".-..-."}, {':', "---..."},
    {';', "-.-.-."}, {'@', ".--.-."}, {'!', "-.-.--"}, {'&', ".-..."},
};
struct Pro
{
    const char *name;
    const char *m;
};
const Pro kProsigns[] = {
    {"AR", ".-.-."},
    {"SK", "...-.-"},
    {"BT", "-...-"},
    {"KN", "-.--."},
    {"AS", ".-..."},
    {"BK", "-...-.-"},
    {"CT", "-.-.-"},
    {"SN", "...-."},
    {"HH", "........"},
};
} // namespace

namespace cw
{
const char *morse(char c)
{
    const char u = char(std::toupper(static_cast<unsigned char>(c)));
    for (const Code &k : kCodes)
        if (k.c == u)
            return k.m;
    return nullptr;
}

const char *prosign(const std::string &name)
{
    std::string u;
    for (char c : name)
        u.push_back(char(std::toupper(static_cast<unsigned char>(c))));
    for (const Pro &p : kProsigns)
        if (u == p.name)
            return p.m;
    return nullptr;
}
} // namespace cw

void CwModulator::setWpm(double wpm)
{
    m_wpm = std::clamp(wpm, 5.0, 60.0);
}

void CwModulator::clear()
{
    m_q.clear();
    m_queued = m_sent = 0;
    m_inSeg = false;
    m_left = 0.0;
    m_env = 0.0;
}

void CwModulator::preroll(double sec)
{
    clear();
    m_q.push_back(Seg {false, std::max(0.0, sec), true, false});
}

void CwModulator::tail(double sec)
{
    m_q.push_back(Seg {false, std::max(0.0, sec), true, false});
}

void CwModulator::addGap(double units)
{
    m_q.push_back(Seg {false, units, false, true});
    m_queued += size_t(units);
}

size_t CwModulator::addElements(const char *code)
{
    for (const char *p = code; *p; ++p)
    {
        const double u = *p == '-' ? 3.0 : 1.0;
        m_q.push_back(Seg {true, u, false, true});
        m_queued += size_t(u);
        // element gap, letter gap after the last element
        addGap(p[1] ? 1.0 : 3.0);
    }
    return m_queued;
}

size_t CwModulator::queueChar(char c)
{
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
    {
        addGap(4.0); // + the 3 after the previous letter = 7
        return m_queued;
    }
    const char *m = cw::morse(c);
    return m ? addElements(m) : 0;
}

size_t CwModulator::queueProsign(const std::string &name)
{
    const char *m = cw::prosign(name);
    return m ? addElements(m) : 0;
}

bool CwModulator::idle() const
{
    return m_q.empty() && !m_inSeg;
}

void CwModulator::render(float *out, size_t n, double fs)
{
    if (fs <= 0.0)
    {
        std::fill(out, out + n, 0.0f);
        return;
    }
    const double dot = 1.2 / m_wpm * fs; // samples per dot
    const double step = 1.0 / std::max(1.0, m_riseMs * 1e-3 * fs);
    const double dphi = 2.0 * M_PI * m_pitchHz / fs;
    for (size_t i = 0; i < n; ++i)
    {
        if (!m_inSeg && !m_q.empty())
        {
            m_seg = m_q.front();
            m_q.pop_front();
            m_left += m_seg.secs ? m_seg.units * fs : m_seg.units * dot;
            m_inSeg = true;
        }
        const bool key = m_inSeg && m_seg.on;
        // Linear ramp of the key state, shaped by a raised cosine below. The
        // ramp starts at the element boundary, so marks keep their length.
        m_env = key ? std::min(1.0, m_env + step) : std::max(0.0, m_env - step);
        const double a = 0.5 - 0.5 * std::cos(M_PI * m_env);
        out[i] = float(a * std::sin(m_phase));
        m_phase += dphi;
        if (m_phase > 2.0 * M_PI)
            m_phase -= 2.0 * M_PI;
        if (m_inSeg)
        {
            m_left -= 1.0;
            if (m_left <= 0.0)
            {
                if (m_seg.counted)
                    m_sent += size_t(m_seg.units);
                m_inSeg = false; // keep the fraction in m_left
            }
        }
    }
}
