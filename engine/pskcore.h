#pragma once

// BPSK31 / BPSK63 / BPSK125 (G3PLX PSK31 and its faster variants).
// Qt-free so it can be unit-tested with a plain C++ compiler.
//
// Signal: a carrier whose phase reverses for a 0 bit and stays for a 1 bit,
// with cosine-shaped amplitude through each reversal. Characters are sent in
// Varicode and separated by "00". An idle transmitter sends continuous
// reversals (all zeros).
//
// Receive:
//   mix to baseband at the carrier (with AFC) -> 2-symbol Hann matched filter,
//   evaluated 16 times per symbol -> symbol timing from the averaged envelope
//   (peaks at symbol centres) -> differential phase decision -> Varicode.
//   Quality = mean of cos(2 * phase change): 1 for a clean signal, ~0 for noise.

#include <complex>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace psk
{
enum Speed
{
    Bpsk31 = 0,
    Bpsk63 = 1,
    Bpsk125 = 2
};

double baudFor(int speed); // 31.25, 62.5, 125
const char *speedName(int speed); // "BPSK31", ...

// Varicode for ASCII 0..127 as a string of '0'/'1' (no trailing "00"); nullptr
// for characters outside the table.
const char *varicode(unsigned char c);
// Inverse: code bits (MSB first, without the "00") to the character, or -1.
int varicodeDecode(uint32_t bits);

// Finds the centre of a PSK signal near approxHz: the power-weighted centroid
// of the spectrum within +/- 1.5 baud. Returns approxHz if nothing stands out.
double findCenter(const float *x, size_t n, double sampleRate, double approxHz, double baud);

// Which speed a signal near approxHz is sent at, from a few seconds of audio
// (5 s is plenty). A PSK signal's envelope dips at every phase reversal, and
// reversals can only happen on the symbol clock, so the envelope carries a
// line at the baud rate: 31.25, 62.5 or 125 Hz (and at its multiples, never
// below it). For each speed the signal is filtered to that speed's width and
// the strength of that line is measured; the slowest speed with a clear line
// is the answer. Works on idle
// reversals as well as on text. speed is -1 when there is no clear winner
// (no signal, or not PSK). The frequency is left alone: AFC pulls in from
// within a baud or so.
struct SpeedGuess
{
    int speed = -1;
    // Line strength per speed: power at the baud rate over the envelope's
    // power just beside it. Noise reads about 1 (rarely over 10); a PSK
    // signal hundreds. A clear line is 60 or more.
    double score[3] = {0.0, 0.0, 0.0};
};
SpeedGuess detectSpeed(const float *x, size_t n, double sampleRate, double approxHz);
} // namespace psk

class PskVaricodeDecoder
{
public:
    void reset()
    {
        m_reg = 0;
    }
    // Feed one bit; returns a character (0..127) when one completes, else -1.
    int push(int bit);

private:
    uint32_t m_reg = 0;
};

class PskModulator
{
public:
    void setBaud(double baud);
    void setCarrier(double hz);
    double carrier() const
    {
        return m_carrierHz;
    }

    void clear(); // silence, everything dropped
    // Start: fade in over one symbol, then reversals for sec seconds.
    void preroll(double sec);
    // Queue one character (Varicode + "00"). '\n' is sent as CR LF.
    // Returns the number of counted symbols queued so far after it, or 0 if
    // the character cannot be sent.
    size_t queueChar(char c);
    // sec of reversals, then fade out over one symbol and fall silent.
    void tail(double sec);

    bool idle() const; // nothing queued (idle reversals may be going out)
    size_t unitsSent() const
    {
        return m_sent;
    }
    void render(float *out, size_t n, double sampleRate); // amplitude <= 1

private:
    enum Sym : uint8_t
    {
        Reverse = 0,
        Steady = 1,
        FadeOut = 2,
        FadeIn = 3,
        Counted = 0x80
    };
    void pushBits(const char *bits);

    double m_baud = 31.25;
    double m_carrierHz = 1000.0;
    std::deque<uint8_t> m_q;
    size_t m_queued = 0;
    size_t m_sent = 0;
    bool m_running = false; // between fade-in and fade-out: idle sends reversals
    bool m_inSymbol = false;
    bool m_filler = false; // the current symbol is idle filler
    uint8_t m_sym = Steady;
    double m_t = 0.0; // position in the current symbol, 0..1
    double m_a0 = 0.0; // amplitude at the start of the symbol (+-1 or 0)
    double m_phase = 0.0; // carrier phase, radians
};

class PskDemodulator
{
public:
    struct Stats
    {
        double quality = 0.0; // 0..1
        double levelDb = -120.0; // matched-filter output level, dBFS-ish
        double carrierHz = 0.0; // current (after AFC)
        bool squelchOpen = false;
    };

    void setSampleRate(double fs);
    void setBaud(double baud);
    // Sets the receive frequency and the centre of the AFC range.
    void setCarrier(double hz);
    void setAfc(bool on)
    {
        m_afc = on;
    }
    void setSquelch(double q)
    {
        m_squelch = q;
    }
    void reset();

    // Returns decoded text ('\n' for LF; CR and other control characters dropped).
    std::string process(const float *x, size_t n);
    Stats stats() const;

    // For an RSQ signal report (see psk::rsq*).
    struct Rsq
    {
        // Mean cos(2 x phase change) over the recent symbols that carried the signal (the
        // stronger ones of the last ~1000): 1 = clean, 0 = noise. -1 if too few.
        double phaseQuality = -1.0;
        int symbols = 0;
        // Measured on idle reversals (two tones +/- baud/2): 3rd/5th/7th-order sideband
        // pairs (+/- 1.5, 2.5, 3.5 baud) over the noise between the lines, dB, and the
        // 3rd-order IMD (pair power over the main pair, noise taken off), dB.
        bool idleSeen = false;
        int idleBlocks = 0;
        double side3Db = 0.0, side5Db = 0.0, side7Db = 0.0;
        double imdDb = 0.0; // 3rd order
        bool imdBelowNoise = false; // imdDb is then an upper bound ("better than")
        double imd5Db = -99.0, imd7Db = -99.0; // 5th / 7th-order pairs over the main pair
        double idleSnrDb = 0.0; // main pair over the noise
    };
    Rsq rsq() const;

private:
    void rebuild();
    void onSample(std::complex<double> z);
    void onSymbol(std::complex<double> z);

    double m_fs = 0.0;
    double m_baud = 31.25;
    double m_setHz = 1000.0;
    double m_hz = 1000.0;
    bool m_afc = true;
    double m_squelch = 0.3;

    double m_ncoPhase = 0.0;
    std::vector<std::complex<double>> m_buf;
    std::vector<double> m_win;
    size_t m_pos = 0;
    size_t m_filled = 0;
    double m_step = 1.0; // input samples per filter output (1/16 symbol)
    double m_next = 0.0; // countdown to the next filter output

    double m_bitclk = 0.0;
    double m_sync[16] = {};
    std::complex<double> m_prev {0.0, 0.0};
    std::complex<double> m_coh {0.0, 0.0}; // averaged squared phase change
    double m_quality = 0.0;
    double m_level = 0.0;
    double m_sigLevel = 0.0; // level while locked, for AFC weighting
    PskVaricodeDecoder m_vari;
    std::string m_out;

    // RSQ measurement
    void idleBlockDone();
    double m_nIn = 0.0; // input samples so far
    std::vector<std::pair<float, float>> m_symRing; // (cos 2 dphi, |z|), last kRing symbols
    size_t m_symPos = 0;
    std::vector<std::complex<double>> m_blk; // raw baseband of the block being collected
    double m_blkStart = 0.0;
    struct Block
    {
        double start, end;
        double p[15]; // power at k * baud/2, k = -7..7
    };
    std::vector<Block> m_pending;
    double m_runStart = -1.0; // decision time of the first 0 of the current run of zeros, -1 none
    double m_acc[15] = {}; // smoothed idle-block powers
    int m_idleBlocks = 0;
};

namespace psk
{
// RSQ digits (Readability, Strength, Quality: the PSK signal report).
// R 1..5 from the expected share of characters copied (95 % -> 5, 80 % -> 4, 40 % -> 3,
//   20 % -> 2), which comes from the phase quality (copyFromQuality, measured on this decoder).
// S 1..9 from the trace's SNR: 3 dB -> 1, then one per 3 dB (15 dB -> 5, 27 dB -> 9).
// Q 1..9 from the sideband pairs measured on idle (RSQ: 9 clean, 7 one barely visible pair,
//   5 one easily visible pair, 3 several pairs, 1 splatter). A pair counts as visible when it
//   is over the noise (3 dB) and within ~27 dB of the signal (a waterfall's colour range).
//   3rd-order IMD -30 dB or better -> 9, -27 -> 8, -24 -> 7, -21 -> 6, -18 -> 5, -15 -> 4,
//   worse -> 3; a visible 5th-order pair too -> at most 3 (2 if it is above -20 dB); a
//   visible 7th-order pair as well -> 1.
double copyFromQuality(double phaseQuality); // 0..1
int rsqReadability(double copy);
int rsqStrength(double snrDb);
int rsqQuality(const PskDemodulator::Rsq &m);
} // namespace psk
