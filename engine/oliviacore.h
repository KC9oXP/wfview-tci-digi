#pragma once

// Olivia and Contestia (MFSK with Walsh-Hadamard FEC), receive and transmit,
// using Pawel Jalocha's library as fldigi does (engine/jalocha). Qt-free.
//
// The library works at 8 kHz; audio is resampled to and from the TCI rate.
// Frequencies are the centre of the signal, as fldigi shows them.

#include "audioresampler.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace olivia
{
struct Config
{
    bool contestia = false;
    int tones = 8; // 2, 4, 8, 16, 32, 64, 128, 256
    int bandwidth = 500; // 125, 250, 500, 1000, 2000 Hz
    double centreHz = 1500.0;
    int syncMargin = 8; // tune margin, tone spacings
    int syncInteg = 4; // integration, FEC blocks

    bool operator==(const Config &o) const
    {
        return contestia == o.contestia && tones == o.tones && bandwidth == o.bandwidth && centreHz == o.centreHz
            && syncMargin == o.syncMargin && syncInteg == o.syncInteg;
    }
    bool operator!=(const Config &o) const
    {
        return !(*this == o);
    }
};

// "Olivia 8/500", "Contestia 4/250"
std::string name(const Config &c);
// Characters per second for the configuration.
double charsPerSecond(const Config &c);
} // namespace olivia

class OliviaDemodulator
{
public:
    OliviaDemodulator();
    ~OliviaDemodulator();

    void setSampleRate(double fs);
    void setConfig(const olivia::Config &c);
    // Minimum S/N (as the library reports it) for printing; 0 = print
    // everything (noise prints as random characters). Default 5.
    void setSquelch(double snr)
    {
        m_squelch = snr;
    }
    void reset();

    std::string process(const float *x, size_t n);

    double snr() const
    {
        return m_snr;
    }
    double freqOffsetHz() const
    {
        return m_offset;
    }

private:
    void rebuild();

    struct Impl;
    std::unique_ptr<Impl> d;
    olivia::Config m_cfg;
    double m_fs = 0.0;
    double m_squelch = 5.0;
    double m_snr = 0.0;
    double m_offset = 0.0;
    bool m_escape = false;
    AudioResampler m_rs;
    std::vector<float> m_buf8;
    std::vector<double> m_dbl;
};

class OliviaModulator
{
public:
    OliviaModulator();
    ~OliviaModulator();

    void setConfig(const olivia::Config &c);
    void setCarrier(double hz);
    double carrier() const
    {
        return m_cfg.centreHz;
    }

    void clear();
    void preroll(double sec); // start: sec of zero-data blocks first
    size_t queueChar(char c); // counted characters after c, 0 if dropped
    void tail(double sec); // finish the queued text, then stop
    bool idle() const;
    size_t unitsSent() const
    {
        return m_sent;
    }
    void render(float *out, size_t n, double sampleRate);

private:
    void rebuild();
    void produce(); // one library output batch (8 kHz) into m_out8

    struct Impl;
    std::unique_ptr<Impl> d;
    olivia::Config m_cfg;
    std::deque<uint8_t> m_q;
    size_t m_queued = 0;
    size_t m_sent = 0;
    size_t m_prerollChars = 0;
    bool m_running = false;
    bool m_stopReq = false;
    bool m_stopped = true;
    double m_outRate = 0.0;
    AudioResampler m_rs;
    std::vector<float> m_out8, m_outFs;
    size_t m_outPos = 0;
};
