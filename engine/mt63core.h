#pragma once

// MT63 (64 carriers, Walsh-function FEC, long time/frequency interleave),
// receive and transmit, using Pawel Jalocha's MT63 code as fldigi carries it
// (engine/mt63). Qt-free.
//
// MT63-500, -1000 and -2000 (bandwidth in Hz; 5, 10 and 20 characters per
// second), short (32) or long (64) interleave. Long interleave is the usual
// choice on HF nets (MT63-1KL). The code works at 8 kHz; audio is resampled
// to and from the TCI rate. Frequencies are the centre of the signal.

#include "audioresampler.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace mt63
{
struct Config
{
    int bandwidth = 1000; // 500, 1000, 2000 Hz
    bool longInterleave = true; // 64 symbols ("L"), else 32 ("S")
    double centreHz = 1500.0;
    bool longIntegration = false; // receiver sync integration 32 instead of 16 (slower, steadier)

    bool operator==(const Config &o) const
    {
        return bandwidth == o.bandwidth && longInterleave == o.longInterleave && centreHz == o.centreHz
            && longIntegration == o.longIntegration;
    }
    bool operator!=(const Config &o) const
    {
        return !(*this == o);
    }
};

// "MT63-1000L", "MT63-500S"
std::string name(const Config &c);
double charsPerSecond(const Config &c); // bandwidth / 100
// The lowest and highest centre frequencies the code accepts for a bandwidth
// (the signal must stay inside 100 Hz .. 4 kHz at the code's 8 kHz rate,
// and inside the radio's passband).
double minCentreHz(int bandwidth);
double maxCentreHz(int bandwidth);
// Time from a character going in to it coming out of a receiver: the interleave.
double delaySeconds(const Config &c);
} // namespace mt63

class MT63rx;
class MT63tx;

class Mt63Demodulator
{
public:
    Mt63Demodulator();
    ~Mt63Demodulator();

    void setSampleRate(double fs);
    void setConfig(const mt63::Config &c);
    // Minimum S/N in dB (as the decoder's FEC sees it) for printing; 0 (or
    // below) = off, everything prints (noise prints as random characters).
    // Default 5.5 dB (noise alone reads up to about 5).
    void setSquelch(double db)
    {
        m_squelch = db;
    }
    void reset();

    std::string process(const float *x, size_t n);

    double snrDb() const
    {
        return m_snrDb;
    }
    bool locked() const
    {
        return m_locked;
    }
    // Squelch level (snrDb, 0 while not locked) and whether it is printing.
    double squelchLevel() const
    {
        return m_locked ? std::max(0.0, m_snrDb) : 0.0;
    }
    bool squelchOpen() const
    {
        return !(m_squelch > 0.0) || m_printing;
    }
    double freqOffsetHz() const
    {
        return m_offset;
    }

private:
    void rebuild();

    std::unique_ptr<MT63rx> m_rx;
    mt63::Config m_cfg;
    double m_fs = 0.0;
    double m_squelch = 5.5;
    double m_snrDb = -99.0;
    bool m_locked = false;
    double m_offset = 0.0;
    bool m_escape = false;
    double m_aboveSec = 0.0; // how long the S/N has been above the squelch
    double m_hangSec = 0.0; // printing goes on this long after it falls
    bool m_printing = false;
    int m_zeroRun = 0; // zero characters in a row
    AudioResampler m_rs;
    std::vector<float> m_buf8;
};

class Mt63Modulator
{
public:
    Mt63Modulator();
    ~Mt63Modulator();

    void setConfig(const mt63::Config &c);
    void setCarrier(double hz);
    double carrier() const
    {
        return m_cfg.centreHz;
    }

    void clear();
    void preroll(double sec); // start: sec of zero characters first (at least one interleave)
    size_t queueChar(char c); // counted characters after c, 0 if dropped
    void tail(double sec); // finish the queued text (and flush the interleave), then stop
    bool idle() const;
    size_t unitsSent() const
    {
        return m_sent;
    }
    void render(float *out, size_t n, double sampleRate);

private:
    void rebuild();
    void produce(); // one character time (8 kHz) into m_out8

    std::unique_ptr<MT63tx> m_tx;
    mt63::Config m_cfg;
    std::deque<int> m_q; // characters, -1 marks a counted one's end
    size_t m_queued = 0;
    size_t m_sent = 0;
    std::deque<bool> m_inFlight; // per character in the interleave: counted?
    int m_flushLeft = 0;
    bool m_running = false;
    bool m_stopReq = false;
    bool m_stopped = true;
    double m_peak = 0.0;
    double m_outRate = 0.0;
    AudioResampler m_rs;
    std::vector<float> m_out8, m_outFs;
    size_t m_outPos = 0;
};
