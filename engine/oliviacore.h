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
// One FEC block (64 symbols, Contestia 32), seconds.
double blockSeconds(const Config &c);
// Zero characters sent before the text so a receiver has found the signal
// before the first letter: two FEC blocks, 1..4 s.
double prerollSeconds(const Config &c);
} // namespace olivia

class OliviaDemodulator
{
public:
    OliviaDemodulator();
    ~OliviaDemodulator();

    void setSampleRate(double fs);
    void setConfig(const olivia::Config &c);
    // Squelch: a decoded FEC block is printed when its own S/N (the library's
    // S/N for that block alone, not the average over several blocks that
    // snr() shows, so it falls the moment the text ends) is at least this.
    // inDb: the same S/N in dB (20 log10). 0 = print everything (noise prints
    // as random characters). Default 6 (15.5 dB); noise reads up to about 3,
    // other signals in the passband (an RTTY pair) about 5.
    // It opens on the second good block in a row once the averaged S/N
    // (snr()) has reached the setting too, so other signals in the passband
    // do not open it (the blocks waiting are printed then; the first only if
    // it had at least half the S/N of the block that opens it: the block that
    // catches the start of a transmission is only partly signal). Up to four weak
    // blocks in a row are held back and printed when a good block follows (a
    // fade: the text through it is printed as decoded, as with the squelch
    // off); a fifth, or a transmission that ends, drops them and closes it.
    void setSquelch(double threshold, bool inDb = false)
    {
        m_squelch = threshold;
        m_squelchDb = inDb;
    }
    void reset();

    std::string process(const float *x, size_t n);

    double snr() const
    {
        return m_snr;
    }
    double blockSnr() const
    {
        return m_blockSnr;
    }
    // What the squelch compares (blockSnr, or it in dB with the dB box) and
    // whether it is printing.
    double squelchLevel() const
    {
        return m_squelchDb ? squelchDbOf(m_blockSnr) : m_blockSnr;
    }
    bool squelchOpen() const
    {
        return !(m_squelch > 0.0) || m_open;
    }
    static double squelchDbOf(double snr);
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
    double m_squelch = 6.0;
    bool m_squelchDb = false;
    static constexpr int kBridgeBlocks = 4; // weak blocks in a row bridged (a fade)
    bool m_open = false; // last block printed
    static constexpr int kOpeningBlocks = 6; // good blocks waiting for the average
    bool m_opening = false; // good blocks seen while closed (m_first, then m_held)
    int m_openingBlocks = 0;
    std::string m_first; // the first of them
    double m_heldSnr = 0.0; // its S/N
    int m_belowRun = 0; // blocks in a row below the squelch
    std::string m_held; // one weak block waiting for the next
    double m_snr = 0.0;
    double m_blockSnr = 0.0;
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
