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
};
