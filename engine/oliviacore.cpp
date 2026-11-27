#include "oliviacore.h"

#include "jalocha/pj_mfsk.h"
#include "squelch.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace
{
constexpr double kLibRate = 8000.0;

double firstCarrierMultiplier(const olivia::Config &c)
{
    // fldigi: the library wants the lowest tone; the user sets the centre.
    const double fcOffset = c.bandwidth * (1.0 - 0.5 / c.tones) / 2.0;
    return (c.centreHz - fcOffset) / 500.0;
}
} // namespace

namespace olivia
{
std::string name(const Config &c)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s %d/%d", c.contestia ? "Contestia" : "Olivia", c.tones, c.bandwidth);
    return buf;
}

double charsPerSecond(const Config &c)
{
    MFSK_Transmitter<double> tx;
    tx.bContestia = c.contestia;
    tx.Tones = size_t(c.tones);
    tx.Bandwidth = size_t(c.bandwidth);
    tx.SampleRate = kLibRate;
    tx.OutputSampleRate = kLibRate;
    tx.FirstCarrierMultiplier = 3.0f;
    tx.Reverse = 0;
    if (tx.Preset() < 0)
        return 1.0;
    return tx.CharactersPerSecond();
}

double blockSeconds(const Config &c)
{
    return (c.contestia ? 32.0 : 64.0) * c.tones / std::max(1, c.bandwidth);
}

double prerollSeconds(const Config &c)
{
    return std::clamp(2.0 * blockSeconds(c), 1.0, 4.0);
}
} // namespace olivia

// ===========================================================================
// Receiver
// ===========================================================================

struct OliviaDemodulator::Impl
{
    MFSK_Receiver<double> rx;
};

OliviaDemodulator::OliviaDemodulator()
    : d(std::make_unique<Impl>())
{
    rebuild();
}

OliviaDemodulator::~OliviaDemodulator() = default;

void OliviaDemodulator::setSampleRate(double fs)
{
    if (fs == m_fs)
        return;
    m_fs = fs;
    m_rs.setRates(fs, kLibRate);
    m_rs.reset();
}

void OliviaDemodulator::setConfig(const olivia::Config &c)
{
    if (c == m_cfg)
        return;
    const bool onlyFreq = c.contestia == m_cfg.contestia && c.tones == m_cfg.tones && c.bandwidth == m_cfg.bandwidth
        && c.syncMargin == m_cfg.syncMargin && c.syncInteg == m_cfg.syncInteg;
    m_cfg = c;
    if (onlyFreq)
    {
        // As fldigi: a new carrier re-presets the receiver.
        d->rx.FirstCarrierMultiplier = float(firstCarrierMultiplier(m_cfg));
        d->rx.Preset();
        d->rx.SyncThreshold = 0.0; // every block comes out; process() squelches
        return;
    }
    rebuild();
}

void OliviaDemodulator::rebuild()
{
    d = std::make_unique<Impl>();
    MFSK_Receiver<double> &rx = d->rx;
    rx.bContestia = m_cfg.contestia;
    rx.Tones = size_t(m_cfg.tones);
    rx.Bandwidth = size_t(m_cfg.bandwidth);
    rx.SyncMargin = size_t(m_cfg.syncMargin);
    rx.SyncIntegLen = size_t(m_cfg.syncInteg);
    rx.SampleRate = kLibRate;
    rx.InputSampleRate = kLibRate;
    rx.FirstCarrierMultiplier = float(firstCarrierMultiplier(m_cfg));
    rx.Reverse = 0;
    rx.Preset();
    rx.SyncThreshold = 0.0; // after Preset, which raises it to 3; process() squelches
    m_escape = false;
    m_open = false;
    m_opening = false;
    m_belowRun = 0;
    m_held.clear();
}

void OliviaDemodulator::reset()
{
    d->rx.Reset();
    m_escape = false;
    m_open = false;
    m_opening = false;
    m_belowRun = 0;
    m_held.clear();
}

double OliviaDemodulator::squelchDbOf(double snr)
{
    return std::max(0.0, squelch::ampDb(snr));
}

std::string OliviaDemodulator::process(const float *x, size_t n)
{
    std::string out;
    if (m_fs <= 0.0)
        return out;
    m_buf8.clear();
    m_rs.process(x, n, m_buf8);
    if (m_buf8.empty())
        return out;
    m_dbl.assign(m_buf8.begin(), m_buf8.end());
    MFSK_Receiver<double> &rx = d->rx;
    const size_t blocksBefore = rx.BlockCount();
    rx.Process(m_dbl.data(), m_dbl.size());
    const bool newBlock = rx.BlockCount() != blocksBefore;
    m_snr = rx.SignalToNoiseRatio();
    m_blockSnr = rx.BlockSignalToNoiseRatio();
    m_offset = rx.FrequencyOffset();
    std::string text;
    uint8_t ch = 0;
    while (rx.GetChar(ch) > 0)
    {
        int c = ch;
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
        if (c == '\n' || (c >= 32 && c != 127))
            text.push_back(char(c));
    }
    if (!(m_squelch > 0.0))
        return text;
    // Characters only come out when a block is considered (once per block);
    // the decision is made once per block, on that block's own S/N.
    if (!newBlock)
    {
        if (m_open)
            out = text;
        return out;
    }
    if (squelchLevel() >= m_squelch)
    {
        if (m_open)
            out = m_held + text; // m_held: a weak block bridged
        else if (m_opening)
        {
            // Second good block: open. The first is printed too unless it was
            // much weaker than this one (only partly signal: the block that
            // catches the start of a transmission prints a junk letter).
            out = (m_heldSnr >= 0.5 * m_blockSnr ? m_held : std::string()) + text;
            m_open = true;
            m_opening = false;
        }
        else
        {
            m_opening = true; // first good block: wait for the next
            m_held = text;
            m_heldSnr = m_blockSnr;
            return out;
        }
        m_held.clear();
        m_belowRun = 0;
    }
    else if (m_open && ++m_belowRun <= kBridgeBlocks)
        m_held += text; // keep it: printed if a good block follows (a fade)
    else
    {
        m_held.clear();
        m_open = false;
        m_opening = false;
        m_belowRun = 0;
    }
    return out;
}

// ===========================================================================
// Transmitter
// ===========================================================================

struct OliviaModulator::Impl
{
    MFSK_Transmitter<double> tx;
    std::vector<double> buf;
};

OliviaModulator::OliviaModulator()
    : d(std::make_unique<Impl>())
{
    rebuild();
}

OliviaModulator::~OliviaModulator() = default;

void OliviaModulator::setConfig(const olivia::Config &c)
{
    if (c == m_cfg)
        return;
    m_cfg = c;
    if (!m_running)
        rebuild();
}

void OliviaModulator::setCarrier(double hz)
{
    if (hz == m_cfg.centreHz)
        return;
    m_cfg.centreHz = hz;
    if (!m_running)
        rebuild();
}

void OliviaModulator::rebuild()
{
    d = std::make_unique<Impl>();
    MFSK_Transmitter<double> &tx = d->tx;
    tx.bContestia = m_cfg.contestia;
    tx.Tones = size_t(m_cfg.tones);
    tx.Bandwidth = size_t(m_cfg.bandwidth);
    tx.SampleRate = kLibRate;
    tx.OutputSampleRate = kLibRate;
    tx.FirstCarrierMultiplier = float(firstCarrierMultiplier(m_cfg));
    tx.Reverse = 0;
    tx.Preset();
    d->buf.assign(tx.MaxOutputLen + 16, 0.0);
}

void OliviaModulator::clear()
{
    m_q.clear();
    m_queued = 0;
    m_sent = 0;
    m_running = false;
    m_stopReq = false;
    m_stopped = true;
    m_out8.clear();
    m_outFs.clear();
    m_outPos = 0;
    m_rs.reset();
    rebuild();
}

void OliviaModulator::preroll(double sec)
{
    clear();
    d->tx.Start();
    d->tx.PutChar(0); // the transmitter needs one character to begin
    const size_t zeros = size_t(std::lround(sec * olivia::charsPerSecond(m_cfg)));
    for (size_t i = 0; i < zeros; ++i)
        m_q.push_back(0);
    m_running = true;
    m_stopped = false;
}

size_t OliviaModulator::queueChar(char c)
{
    if (!m_running || m_stopReq)
        return 0;
    const auto push = [this](uint8_t v)
    {
        m_q.push_back(v);
        ++m_queued;
    };
    unsigned char u = static_cast<unsigned char>(c);
    if (u == '\n')
    {
        push('\r');
        push('\n');
        return m_queued;
    }
    if (u > 127)
        u = '.';
    if (u < 32)
        return 0;
    if (m_cfg.contestia)
        u = static_cast<unsigned char>(std::toupper(u));
    push(u);
    return m_queued;
}

void OliviaModulator::tail(double)
{
    m_stopReq = true;
}

bool OliviaModulator::idle() const
{
    if (!m_running)
        return true;
    if (m_stopReq)
        return m_stopped;
    // Every queued character has gone into a transmitted block.
    return m_q.empty() && m_sent >= m_queued;
}

void OliviaModulator::produce()
{
    MFSK_Transmitter<double> &tx = d->tx;
    if (m_stopReq && m_q.empty())
        tx.Stop();
    else if (tx.GetReadReady() < tx.BitsPerSymbol)
    {
        if (!m_q.empty())
        {
            tx.PutChar(m_q.front());
            m_q.pop_front();
        }
        else
            tx.PutChar(0);
    }
    const int len = tx.Output(d->buf.data());
    for (int i = 0; i < len; ++i)
        m_out8.push_back(float(0.9 * d->buf[size_t(i)]));
    uint8_t ch = 0;
    while (tx.GetChar(ch) > 0)
        if (ch != 0)
            ++m_sent;
    if (!tx.Running())
        m_stopped = true;
}

void OliviaModulator::render(float *out, size_t n, double fs)
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
