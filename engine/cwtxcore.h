#pragma once

// CW transmit as audio: a keyed tone sent as TCI TX audio with the radio in a
// data/SSB mode (like fldigi's CW). On the air it is an ordinary CW signal at
// the dial frequency + the tone pitch. Qt-free.
//
// Timing is PARIS: a dot lasts 1.2 / WPM seconds; dash 3, element gap 1,
// letter gap 3, word gap 7 dots. Edges are raised-cosine (5 ms default) so
// the keying has no clicks. Prosigns are written <AR>, <SK>, <BT>, <KN>, <AS>,
// <BK> (sent run together); '=' is <BT> and '+' is <AR>.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace cw
{
// Morse for one character as '.'/'-', or nullptr. Lower case maps to upper.
const char *morse(char c);
// Morse for a prosign name such as "SK", or nullptr.
const char *prosign(const std::string &name);
} // namespace cw

class CwModulator
{
public:
    void setWpm(double wpm);
    double wpm() const
    {
        return m_wpm;
    }
    void setPitch(double hz)
    {
        m_pitchHz = hz;
    }
    double pitch() const
    {
        return m_pitchHz;
    }
    void setRiseMs(double ms)
    {
        m_riseMs = ms;
    }

    void clear();
    void preroll(double sec); // key-up silence (lets PTT settle)
    // Queue one character (' ' and '\n' are word gaps); returns the counted
    // dot-units queued so far after it, or 0 if it cannot be sent.
    size_t queueChar(char c);
    // Queue a prosign by name ("SK", "AR", ...); same return as queueChar.
    size_t queueProsign(const std::string &name);
    void tail(double sec);
    bool idle() const;
    size_t unitsSent() const
    {
        return m_sent;
    }
    void render(float *out, size_t n, double sampleRate);

private:
    struct Seg
    {
        bool on;
        double units; // dot lengths (or seconds when secs is true)
        bool secs;
        bool counted;
    };
    size_t addElements(const char *code);
    void addGap(double units);

    double m_wpm = 20.0;
    double m_pitchHz = 700.0;
    double m_riseMs = 5.0;
    std::deque<Seg> m_q;
    size_t m_queued = 0;
    size_t m_sent = 0;
    bool m_inSeg = false;
    Seg m_seg {false, 0.0, false, false};
    double m_left = 0.0; // samples left in the current segment
    double m_env = 0.0; // 0..1 keying envelope (before shaping)
    double m_phase = 0.0;
};
