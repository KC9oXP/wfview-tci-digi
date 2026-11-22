#include "mt63core.h"

#include "mt63/dsp.h"
#include "mt63/mt63base.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
constexpr double kLibRate = 8000.0;
constexpr size_t kRxBlock = 512; // samples per call into the receiver, as fldigi's flush
} // namespace

namespace mt63
{
std::string name(const Config &c)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "MT63-%d%s", c.bandwidth, c.longInterleave ? "L" : "S");
    return buf;
}

double charsPerSecond(const Config &c)
{
    return c.bandwidth / 100.0;
}

double minCentreHz(int bandwidth)
{
    return std::max(200.0 + bandwidth / 2.0, 500.0);
}

double maxCentreHz(int bandwidth)
{
    return 3000.0 - bandwidth / 2.0;
}

double delaySeconds(const Config &c)
{
    return (c.longInterleave ? 64.0 : 32.0) / charsPerSecond(c);
}
} // namespace mt63

// ===========================================================================
// Receiver
// ===========================================================================

Mt63Demodulator::Mt63Demodulator()
{
    rebuild();
}

Mt63Demodulator::~Mt63Demodulator() = default;

void Mt63Demodulator::setSampleRate(double fs)
{
    if (fs == m_fs)
        return;
    m_fs = fs;
    m_rs.setRates(fs, kLibRate);
    m_rs.reset();
}

void Mt63Demodulator::setConfig(const mt63::Config &c)
{
    if (c == m_cfg)
        return;
    m_cfg = c;
    rebuild();
}

void Mt63Demodulator::rebuild()
{
    m_rx = std::make_unique<MT63rx>();
    const double lo = mt63::minCentreHz(m_cfg.bandwidth), hi = mt63::maxCentreHz(m_cfg.bandwidth);
    m_rx->Preset(std::clamp(m_cfg.centreHz, lo, hi),
                 m_cfg.bandwidth,
                 m_cfg.longInterleave ? 1 : 0,
                 m_cfg.longIntegration ? 32 : 16);
    m_escape = false;
    m_snrDb = -99.0;
    m_locked = false;
    m_offset = 0.0;
    m_aboveSec = 0.0;
    m_hangSec = 0.0;
    m_printing = false;
    m_zeroRun = 0;
}

void Mt63Demodulator::reset()
{
    rebuild();
}

std::string Mt63Demodulator::process(const float *x, size_t n)
{
    std::string out;
    if (m_fs <= 0.0 || !x || n == 0)
        return out;
    m_rs.process(x, n, m_buf8);
    size_t used = 0;
    double_buff in;
    while (m_buf8.size() - used >= kRxBlock)
    {
        if (in.EnsureSpace(int(kRxBlock)) < 0)
            break;
        for (size_t i = 0; i < kRxBlock; ++i)
            in.Data[i] = double(m_buf8[used + i]);
        in.Len = int(kRxBlock);
        used += kRxBlock;
        m_rx->Process(&in);
        const double snr = m_rx->FEC_SNR();
        m_snrDb = 10.0 * std::log10(std::max(snr, 1e-3));
        m_locked = m_rx->SYNC_LockStatus() != 0;
        m_offset = m_rx->TotalFreqOffset();
        // Squelch. The decoder prints random characters while it locks on
        // (its first block is only partly signal) and the last characters come
        // out of the de-interleaver up to one interleave after the S/N has
        // started to fall at the end. A sender sends one interleave of zero
        // characters before and after the text, which print nothing. So:
        // nothing for the first 3/4 interleave after the S/N rises above the
        // squelch, and printing carries on for up to one interleave after it
        // falls, until the first closing zero character.
        const double dt = double(kRxBlock) / kLibRate;
        const double interleave = mt63::delaySeconds(m_cfg);
        // (Time above counts up while above and down while below, so a weak
        // signal that dips now and then still opens it.)
        const bool above = m_locked && m_snrDb >= m_squelch;
        if (above)
        {
            m_aboveSec = std::min(m_aboveSec + dt, 0.75 * interleave);
            m_hangSec = interleave;
        }
        else
        {
            m_aboveSec = std::max(0.0, m_aboveSec - dt);
            m_hangSec = std::max(0.0, m_hangSec - dt);
        }
        if (above && m_aboveSec >= 0.75 * interleave)
            m_printing = true;
        else if (!above && (m_hangSec <= 0.0 || m_zeroRun >= 3))
            m_printing = false;
        const bool open = m_squelch <= -99.0 || m_printing;
        for (int i = 0; i < m_rx->Output.Len; ++i)
        {
            int c = static_cast<unsigned char>(m_rx->Output.Data[i]);
            // fldigi's 8-bit escape: 127 then the low 7 bits.
            if (m_escape)
            {
                m_escape = false;
                c += 128;
            }
            else if (c == 127)
            {
                m_escape = true;
                continue;
            }
            // Zero characters: the sender's opening and closing fill (and
            // idle). Once the S/N has fallen, a run of them means the text is
            // complete: stop there.
            if (c == 0)
            {
                ++m_zeroRun;
                if (m_printing && m_zeroRun >= 3 && !above)
                {
                    m_printing = false;
                    m_hangSec = 0.0;
                }
                continue;
            }
            m_zeroRun = 0;
            if (!open)
                continue;
            if (c == '\n' || (c >= 32 && c != 127))
                out.push_back(char(c));
        }
    }
    m_buf8.erase(m_buf8.begin(), m_buf8.begin() + std::ptrdiff_t(used));
    return out;
}

// ===========================================================================
// Transmitter
// ===========================================================================

Mt63Modulator::Mt63Modulator()
{
    rebuild();
}

Mt63Modulator::~Mt63Modulator() = default;

void Mt63Modulator::setConfig(const mt63::Config &c)
{
    if (c == m_cfg)
        return;
    m_cfg = c;
    if (!m_running)
        rebuild();
}

void Mt63Modulator::setCarrier(double hz)
{
    if (hz == m_cfg.centreHz)
        return;
    m_cfg.centreHz = hz;
    if (!m_running)
        rebuild();
}

void Mt63Modulator::rebuild()
{
    m_tx = std::make_unique<MT63tx>();
    const double lo = mt63::minCentreHz(m_cfg.bandwidth), hi = mt63::maxCentreHz(m_cfg.bandwidth);
    m_tx->Preset(std::clamp(m_cfg.centreHz, lo, hi), m_cfg.bandwidth, m_cfg.longInterleave ? 1 : 0);
}

void Mt63Modulator::clear()
{
    m_q.clear();
    m_queued = 0;
    m_sent = 0;
    m_inFlight.clear();
    m_flushLeft = 0;
    m_running = false;
    m_stopReq = false;
    m_stopped = true;
    m_peak = 0.0;
    m_out8.clear();
    m_outFs.clear();
    m_outPos = 0;
    m_rs.reset();
    rebuild();
}

void Mt63Modulator::preroll(double sec)
{
    clear();
    // fldigi sends one interleave of zeros first, so the receiver has the
    // whole first block; a longer preroll adds more.
    const size_t zeros
        = std::max<size_t>(size_t(m_tx->DataInterleave), size_t(std::lround(sec * mt63::charsPerSecond(m_cfg))));
    for (size_t i = 0; i < zeros; ++i)
        m_q.push_back(0);
    m_running = true;
    m_stopped = false;
}

size_t Mt63Modulator::queueChar(char c)
{
    if (!m_running || m_stopReq)
        return 0;
    unsigned char u = static_cast<unsigned char>(c);
    const auto push = [this](int v)
    {
        m_q.push_back(v);
        m_q.push_back(-1); // counted
        ++m_queued;
    };
    if (u == '\n')
    {
        m_q.push_back('\r');
        push('\n');
        return m_queued;
    }
    if (u > 127)
        u = '.';
    if (u < 32 || u == 127)
        return 0;
    push(u);
    return m_queued;
}

void Mt63Modulator::tail(double)
{
    m_stopReq = true;
}

bool Mt63Modulator::idle() const
{
    if (!m_running)
        return true;
    if (m_stopReq)
        return m_stopped;
    return m_q.empty() && m_sent >= m_queued;
}

void Mt63Modulator::produce()
{
    int ch = 0;
    bool counted = false;
    if (!m_q.empty())
    {
        ch = m_q.front();
        m_q.pop_front();
        if (!m_q.empty() && m_q.front() == -1)
        {
            counted = true;
            m_q.pop_front();
        }
    }
    else if (m_stopReq)
    {
        // Text done: flush the interleave with zeros, then the jamming symbol.
        if (m_flushLeft == 0)
            m_flushLeft = m_tx->DataInterleave;
        if (--m_flushLeft == 0)
        {
            m_tx->SendJam();
            m_stopped = true;
            ch = -2;
        }
    }
    if (ch != -2)
        m_tx->SendChar(char(ch));
    // A character is all on the air one interleave after it went in.
    m_inFlight.push_back(counted);
    while (int(m_inFlight.size()) > m_tx->DataInterleave)
    {
        if (m_inFlight.front())
            ++m_sent;
        m_inFlight.pop_front();
    }
    if (m_stopped)
    {
        for (bool c : m_inFlight)
            if (c)
                ++m_sent;
        m_inFlight.clear();
    }
    const int len = m_tx->Comb.Output.Len;
    const double *d = m_tx->Comb.Output.Data;
    for (int i = 0; i < len; ++i)
        m_peak = std::max(m_peak, std::fabs(d[i]));
    // As fldigi: scaled by the largest sample so far (the first symbols set it).
    const double g = m_peak > 0.0 ? 0.8 / m_peak : 0.0; // resampling adds a little
    for (int i = 0; i < len; ++i)
        m_out8.push_back(float(d[i] * g));
}

void Mt63Modulator::render(float *out, size_t n, double fs)
{
    if (fs != m_outRate)
    {
        m_outRate = fs;
        m_rs.setRates(kLibRate, fs);
        m_rs.reset();
    }
    size_t i = 0;
    while (i < n)
    {
        if (m_outPos >= m_outFs.size())
        {
            m_outFs.clear();
            m_outPos = 0;
            if (!m_running || m_stopped)
                break;
            m_out8.clear();
            produce();
            m_rs.process(m_out8.data(), m_out8.size(), m_outFs);
            continue;
        }
        const size_t take = std::min(n - i, m_outFs.size() - m_outPos);
        std::copy(
            m_outFs.begin() + std::ptrdiff_t(m_outPos), m_outFs.begin() + std::ptrdiff_t(m_outPos + take), out + i);
        m_outPos += take;
        i += take;
    }
    std::fill(out + i, out + n, 0.0f);
}
