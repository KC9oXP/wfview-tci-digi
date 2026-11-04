#include "pskcore.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
// Varicode, ASCII 0..127 (G3PLX).
const char *const kVaricode[128] = {
    "1010101011", "1011011011", "1011101101", "1101110111", "1011101011", "1101011111", "1011101111", "1011111101",
    "1011111111", "11101111",   "11101",      "1101101111", "1011011101", "11111",      "1101110101", "1110101011",
    "1011110111", "1011110101", "1110101101", "1110101111", "1101011011", "1101101011", "1101101101", "1101010111",
    "1101111011", "1101111101", "1110110111", "1101010101", "1101011101", "1110111011", "1011111011", "1101111111",
    "1",          "111111111",  "101011111",  "111110101",  "111011011",  "1011010101", "1010111011", "101111111",
    "11111011",   "11110111",   "101101111",  "111011111",  "1110101",    "110101",     "1010111",    "110101111",
    "10110111",   "10111101",   "11101101",   "11111111",   "101110111",  "101011011",  "101101011",  "110101101",
    "110101011",  "110110111",  "11110101",   "110111101",  "111101101",  "1010101",    "111010111",  "1010101111",
    "1010111101", "1111101",    "11101011",   "10101101",   "10110101",   "1110111",    "11011011",   "11111101",
    "101010101",  "1111111",    "111111101",  "101111101",  "11010111",   "10111011",   "11011101",   "10101011",
    "11010101",   "111011101",  "10101111",   "1101111",    "1101101",    "101010111",  "110110101",  "101011101",
    "101110101",  "101111011",  "1010101101", "111110111",  "111101111",  "111111011",  "1010111111", "101101101",
    "1011011111", "1011",       "1011111",    "101111",     "101101",     "11",         "111101",     "1011011",
    "101011",     "1101",       "111101011",  "10111111",   "11011",      "111011",     "1111",       "111",
    "111111",     "110111111",  "10101",      "10111",      "101",        "110111",     "1111011",    "1101011",
    "11011111",   "1011101",    "111010101",  "1010110111", "110111011",  "1010110101", "1011010111", "1110110101",
};

uint32_t codeBits(const char *s)
{
    uint32_t v = 0;
    for (; *s; ++s)
        v = (v << 1) | uint32_t(*s == '1');
    return v;
}
} // namespace

namespace psk
{
double baudFor(int speed)
{
    switch (speed)
    {
    case Bpsk63:
        return 62.5;
    case Bpsk125:
        return 125.0;
    default:
        return 31.25;
    }
}

const char *speedName(int speed)
{
    switch (speed)
    {
    case Bpsk63:
        return "BPSK63";
    case Bpsk125:
        return "BPSK125";
    default:
        return "BPSK31";
    }
}

const char *varicode(unsigned char c)
{
    return c < 128 ? kVaricode[c] : nullptr;
}

int varicodeDecode(uint32_t bits)
{
    // Built once, thread-safely (static local initialisation); several
    // decoders may run on different threads.
    static const std::vector<int> table = []()
    {
        std::vector<int> t(4096, -1); // all codes are at most 10 bits
        for (int c = 0; c < 128; ++c)
            t[codeBits(kVaricode[c])] = c;
        return t;
    }();
    return bits < table.size() ? table[bits] : -1;
}

double findCenter(const float *x, size_t n, double fs, double approxHz, double baud)
{
    if (!x || n < 16 || fs <= 0.0)
        return approxHz;
    // Decimate to about 1 Hz resolution windows is not needed: a direct DFT
    // over a narrow range with a Hann window is cheap enough here.
    const double span = 1.5 * baud;
    const double stepHz = std::max(0.5, baud / 40.0);
    const int bins = int(2.0 * span / stepHz) + 1;
    std::vector<double> win(n);
    for (size_t i = 0; i < n; ++i)
        win[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(n - 1));
    std::vector<double> pow(size_t(bins), 0.0);
    double total = 0.0, peak = 0.0;
    for (int b = 0; b < bins; ++b)
    {
        const double f = approxHz - span + b * stepHz;
        const double w = 2.0 * M_PI * f / fs;
        // Goertzel
        double s1 = 0.0, s2 = 0.0;
        const double coeff = 2.0 * std::cos(w);
        for (size_t i = 0; i < n; ++i)
        {
            const double s0 = x[i] * win[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        pow[size_t(b)] = p;
        total += p;
        peak = std::max(peak, p);
    }
    if (total <= 0.0)
        return approxHz;
    // Ignore the floor: only bins above a quarter of the peak count.
    const double mean = total / bins;
    if (peak < 4.0 * mean)
        return approxHz; // flat: nothing there
    double sw = 0.0, sf = 0.0;
    for (int b = 0; b < bins; ++b)
    {
        const double p = pow[size_t(b)] - 0.25 * peak;
        if (p <= 0.0)
            continue;
        sw += p;
        sf += p * (approxHz - span + b * stepHz);
    }
    return sw > 0.0 ? sf / sw : approxHz;
}
} // namespace psk

// ---------------------------------------------------------------------------

int PskVaricodeDecoder::push(int bit)
{
    m_reg = (m_reg << 1) | uint32_t(bit & 1);
    if ((m_reg & 3u) == 0)
    {
        const uint32_t code = m_reg >> 2;
        m_reg = 0;
        if (code == 0)
            return -1;
        return psk::varicodeDecode(code);
    }
    if (m_reg > 0xFFFFu)
        m_reg = 0; // garbage: too long without a gap
    return -1;
}

// ---------------------------------------------------------------------------

void PskModulator::setBaud(double baud)
{
    m_baud = std::clamp(baud, 10.0, 500.0);
}

void PskModulator::setCarrier(double hz)
{
    m_carrierHz = hz;
}

void PskModulator::clear()
{
    m_q.clear();
    m_queued = m_sent = 0;
    m_running = false;
    m_inSymbol = false;
    m_filler = false;
    m_a0 = 0.0;
    m_t = 0.0;
}

void PskModulator::preroll(double sec)
{
    clear();
    m_q.push_back(FadeIn);
    const int n = std::max(1, int(std::lround(sec * m_baud)));
    for (int i = 0; i < n; ++i)
        m_q.push_back(Reverse);
}

void PskModulator::pushBits(const char *bits)
{
    for (const char *p = bits; *p; ++p)
    {
        m_q.push_back(uint8_t((*p == '1' ? Steady : Reverse) | Counted));
        ++m_queued;
    }
    m_q.push_back(uint8_t(Reverse | Counted));
    m_q.push_back(uint8_t(Reverse | Counted));
    m_queued += 2;
}

size_t PskModulator::queueChar(char c)
{
    if (c == '\n')
    {
        pushBits(kVaricode['\r']);
        pushBits(kVaricode['\n']);
        return m_queued;
    }
    const char *code = psk::varicode(static_cast<unsigned char>(c));
    if (!code)
        return 0;
    pushBits(code);
    return m_queued;
}

void PskModulator::tail(double sec)
{
    const int n = std::max(1, int(std::lround(sec * m_baud)));
    for (int i = 0; i < n; ++i)
        m_q.push_back(Reverse);
    m_q.push_back(FadeOut);
}

bool PskModulator::idle() const
{
    return m_q.empty() && (!m_inSymbol || m_filler);
}

void PskModulator::render(float *out, size_t n, double fs)
{
    if (fs <= 0.0)
    {
        std::fill(out, out + n, 0.0f);
        return;
    }
    const double dt = m_baud / fs;
    const double dphi = 2.0 * M_PI * m_carrierHz / fs;
    for (size_t i = 0; i < n; ++i)
    {
        if (!m_inSymbol)
        {
            if (!m_q.empty())
            {
                m_sym = m_q.front();
                m_q.pop_front();
                m_inSymbol = true;
                m_filler = false;
            }
            else if (m_running)
            {
                m_sym = Reverse; // idle: reversals, not counted
                m_inSymbol = true;
                m_filler = true;
            }
        }
        double a = 0.0;
        if (!m_inSymbol)
            m_t = 0.0;
        if (m_inSymbol)
        {
            const double c = std::cos(M_PI * m_t);
            switch (m_sym & 0x7F)
            {
            case Reverse:
                a = m_a0 * c;
                break;
            case Steady:
                a = m_a0;
                break;
            case FadeOut:
                a = m_a0 * 0.5 * (1.0 + c);
                break;
            case FadeIn:
                a = 0.5 * (1.0 - c);
                break;
            }
            m_t += dt;
            if (m_t >= 1.0)
            {
                switch (m_sym & 0x7F)
                {
                case Reverse:
                    m_a0 = -m_a0;
                    break;
                case FadeOut:
                    m_a0 = 0.0;
                    m_running = false;
                    break;
                case FadeIn:
                    m_a0 = 1.0;
                    m_running = true;
                    break;
                default:
                    break;
                }
                if (m_sym & Counted)
                    ++m_sent;
                m_inSymbol = false;
                m_t -= 1.0;
            }
        }
        out[i] = float(a * std::cos(m_phase));
        m_phase += dphi;
        if (m_phase > 2.0 * M_PI)
            m_phase -= 2.0 * M_PI;
    }
}

// ---------------------------------------------------------------------------

void PskDemodulator::setSampleRate(double fs)
{
    if (fs == m_fs)
        return;
    m_fs = fs;
    rebuild();
}

void PskDemodulator::setBaud(double baud)
{
    baud = std::clamp(baud, 10.0, 500.0);
    if (baud == m_baud)
        return;
    m_baud = baud;
    rebuild();
}

void PskDemodulator::setCarrier(double hz)
{
    m_setHz = hz;
    m_hz = hz;
}

void PskDemodulator::rebuild()
{
    if (m_fs <= 0.0)
        return;
    const size_t len = std::max<size_t>(8, size_t(std::lround(2.0 * m_fs / m_baud)));
    m_buf.assign(len, {0.0, 0.0});
    m_win.resize(len);
    double sum = 0.0;
    for (size_t i = 0; i < len; ++i)
    {
        m_win[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double(i) + 0.5) / double(len));
        sum += m_win[i];
    }
    for (double &w : m_win)
        w /= sum;
    m_step = m_fs / (16.0 * m_baud);
    reset();
}

void PskDemodulator::reset()
{
    std::fill(m_buf.begin(), m_buf.end(), std::complex<double>(0.0, 0.0));
    m_pos = 0;
    m_filled = 0;
    m_next = m_step;
    m_bitclk = 0.0;
    std::fill(std::begin(m_sync), std::end(m_sync), 0.0);
    m_prev = {0.0, 0.0};
    m_coh = {0.0, 0.0};
    m_quality = 0.0;
    m_sigLevel = 0.0;
    m_vari.reset();
}

std::string PskDemodulator::process(const float *x, size_t n)
{
    m_out.clear();
    if (m_fs <= 0.0 || m_buf.empty())
        return m_out;
    const size_t len = m_buf.size();
    for (size_t i = 0; i < n; ++i)
    {
        const double v = x[i];
        m_buf[m_pos] = std::complex<double>(v * std::cos(m_ncoPhase), -v * std::sin(m_ncoPhase));
        m_ncoPhase += 2.0 * M_PI * m_hz / m_fs;
        if (m_ncoPhase > 2.0 * M_PI)
            m_ncoPhase -= 2.0 * M_PI;
        m_pos = (m_pos + 1) % len;
        if (m_filled < len)
            ++m_filled;
        m_next -= 1.0;
        if (m_next <= 0.0)
        {
            m_next += m_step;
            if (m_filled < len)
                continue;
            std::complex<double> acc(0.0, 0.0);
            size_t k = m_pos; // oldest sample
            for (size_t j = 0; j < len; ++j)
            {
                acc += m_buf[k] * m_win[j];
                if (++k == len)
                    k = 0;
            }
            onSample(acc);
        }
    }
    return m_out;
}

void PskDemodulator::onSample(std::complex<double> z)
{
    const double mag = std::abs(z);
    const int idx = std::clamp(int(m_bitclk), 0, 15);
    m_sync[idx] = 0.8 * m_sync[idx] + 0.2 * mag;
    m_bitclk += 1.0;
    if (m_bitclk >= 16.0)
    {
        m_bitclk -= 16.0;
        double sum = 0.0, amp = 0.0;
        for (int i = 0; i < 8; ++i)
        {
            sum += m_sync[i] - m_sync[i + 8];
            amp += m_sync[i] + m_sync[i + 8];
        }
        if (amp > 1e-12)
            m_bitclk -= (sum / amp) * 3.0; // nudge the sampling point towards the envelope peak
        onSymbol(z);
    }
}

void PskDemodulator::onSymbol(std::complex<double> z)
{
    const std::complex<double> d = z * std::conj(m_prev);
    m_prev = z;
    const double dm = std::abs(d);
    m_level = 0.9 * m_level + 0.1 * std::abs(z);
    if (dm < 1e-15)
        return;
    const std::complex<double> d2 = (d / dm) * (d / dm);
    // Averaged: its angle is twice the phase drift per symbol (frequency error),
    // its length says how coherent the signal is whatever the drift.
    m_coh = 0.95 * m_coh + 0.05 * d2;
    const double coherence = std::abs(m_coh);
    m_quality = std::max(0.0, m_coh.real());

    const double mag = std::abs(z);
    if (coherence > 0.5)
        m_sigLevel = m_sigLevel <= 0.0 ? mag : 0.98 * m_sigLevel + 0.02 * mag;
    else
        m_sigLevel *= 0.97; // forget a station that has gone, so a weaker one is not gated out
    if (m_afc && coherence > 0.5 && m_sigLevel > 0.0)
    {
        // Weight by how strong this symbol is compared with the locked level so
        // the loop does not wander once the signal has gone.
        const double w = std::min(1.0, (mag / m_sigLevel) * (mag / m_sigLevel));
        const double err = std::arg(m_coh) / 2.0; // radians per symbol
        const double dfHz = err * m_baud / (2.0 * M_PI);
        m_hz += 0.1 * w * (coherence - 0.5) * 2.0 * dfHz;
        const double lim = m_baud;
        m_hz = std::clamp(m_hz, m_setHz - lim, m_setHz + lim);
    }

    const int bit = d.real() > 0.0 ? 1 : 0;
    const int c = m_vari.push(bit);
    // A symbol far below the locked level means the signal has just stopped:
    // drop the partial character instead of printing noise.
    if (m_sigLevel > 0.0 && mag < 0.25 * m_sigLevel)
    {
        m_vari.reset();
        return;
    }
    if (c < 0 || m_quality < m_squelch)
        return;
    if (c == '\n')
        m_out.push_back('\n');
    else if (c == '\t')
        m_out.push_back(' ');
    else if (c >= 32 && c < 127)
        m_out.push_back(char(c));
}

PskDemodulator::Stats PskDemodulator::stats() const
{
    Stats s;
    s.quality = std::max(0.0, m_quality);
    s.levelDb = 20.0 * std::log10(std::max(m_level, 1e-6));
    s.carrierHz = m_hz;
    s.squelchOpen = m_quality >= m_squelch;
    return s;
}
