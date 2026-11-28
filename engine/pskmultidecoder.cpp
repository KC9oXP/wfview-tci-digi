#include "pskmultidecoder.h"

#include "spectrum.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PskMultiDecoder::PskMultiDecoder()
{
    m_win.resize(kFft);
    for (int i = 0; i < kFft; ++i)
        m_win[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * (i + 0.5) / kFft);
    m_tw.resize(kFft / 2);
    for (int k = 0; k < kFft / 2; ++k)
        m_tw[k] = std::polar(1.0, -2.0 * M_PI * k / kFft);
    m_work.resize(kFft);
}

void PskMultiDecoder::setBaud(double baud)
{
    if (std::fabs(baud - m_baud) < 1e-6)
        return;
    m_baud = baud;
    m_channels.clear();
    m_changed = true;
}

void PskMultiDecoder::setRange(double loHz, double hiHz)
{
    m_lo = std::max(100.0, loHz);
    m_hi = std::min(0.45 * kFs, hiHz);
    m_avg.clear();
}

double PskMultiDecoder::rowSquelch() const
{
    if (m_squelchDb)
        return std::max(m_squelch, kMinSquelchDb);
    return std::max(m_squelch, kMinSquelch);
}

void PskMultiDecoder::setSquelch(double threshold, bool inDb)
{
    m_squelch = threshold;
    m_squelchDb = inDb;
    for (auto &c : m_channels)
        c->demod.setSquelch(rowSquelch(), m_squelchDb);
}

void PskMultiDecoder::reset()
{
    m_channels.clear();
    m_hist.clear();
    m_fftIn.clear();
    m_avg.clear();
    m_rs.reset();
    m_changed = true;
}

void PskMultiDecoder::appendText(Channel &c, const std::string &s)
{
    for (char ch : s)
    {
        if (ch == '\n' || ch == '\r')
            ch = ' ';
        if (ch == ' ' && (c.text.empty() || c.text.back() == ' '))
            continue;
        c.text.push_back(ch);
        ++c.total;
    }
    if (c.text.size() > kTextKeep)
        c.text.erase(0, c.text.size() - kTextKeep);
}

bool PskMultiDecoder::feed(const float *x, size_t n, double sampleRate)
{
    if (n == 0 || sampleRate <= 0.0)
        return false;
    if (sampleRate != m_inRate)
    {
        m_inRate = sampleRate;
        m_rs.setRates(sampleRate, kFs);
        reset();
    }
    m_tmp.clear();
    m_rs.process(x, n, m_tmp);
    if (m_tmp.empty())
        return std::exchange(m_changed, false);

    // Channels already open, on the new audio.
    const double sec = double(m_tmp.size()) / kFs;
    for (auto &c : m_channels)
    {
        const std::string out = c->demod.process(m_tmp.data(), m_tmp.size());
        c->ageSec += sec;
        if (!out.empty())
        {
            const auto before = c->total;
            appendText(*c, out);
            m_changed |= c->total != before;
        }
    }

    m_hist.insert(m_hist.end(), m_tmp.begin(), m_tmp.end());
    const size_t keep = size_t(kHistorySec * kFs);
    if (m_hist.size() > keep)
        m_hist.erase(m_hist.begin(), m_hist.end() - std::ptrdiff_t(keep));

    // Spectrum (opens channels, which catch up from m_hist).
    m_fftIn.insert(m_fftIn.end(), m_tmp.begin(), m_tmp.end());
    while (m_fftIn.size() >= size_t(kFft))
    {
        analyse();
        m_fftIn.erase(m_fftIn.begin(), m_fftIn.begin() + kHop);
    }
    return std::exchange(m_changed, false);
}

void PskMultiDecoder::openChannel(double hz, double db)
{
    auto c = std::make_unique<Channel>();
    c->id = ++m_nextId;
    c->hz = hz;
    c->peakDb = db;
    c->demod.setSampleRate(kFs);
    c->demod.setBaud(m_baud);
    c->demod.setCarrier(hz);
    c->demod.setAfc(true);
    c->demod.setSquelch(rowSquelch(), m_squelchDb);
    const std::string out = c->demod.process(m_hist.data(), m_hist.size());
    if (!out.empty())
        appendText(*c, out);
    c->hz = c->demod.stats().carrierHz;
    m_channels.push_back(std::move(c));
    m_changed = true;
}

void PskMultiDecoder::analyse()
{
    const double binHz = kFs / kFft;
    const double hop = double(kHop) / kFs;
    for (int i = 0; i < kFft; ++i)
        m_work[i] = std::complex<double>(m_fftIn[i] * m_win[i], 0.0);
    fftRadix2(m_work, m_tw);

    const int k0 = std::max(1, int(std::floor(m_lo / binHz)));
    const int k1 = std::min(kFft / 2 - 2, int(std::ceil(m_hi / binHz)));
    const int nb = k1 - k0 + 1;
    if (nb < 16)
        return;
    if (int(m_avg.size()) != nb)
    {
        m_avg.assign(nb, 0.0);
        m_avgFresh = true;
    }
    for (int k = 0; k < nb; ++k)
    {
        const double p = std::norm(m_work[k0 + k]);
        m_avg[k] = m_avgFresh ? p : 0.75 * m_avg[k] + 0.25 * p;
    }
    m_avgFresh = false;

    // Power over +/- 0.6 baud around each bin.
    const int w = std::max(1, int(std::lround(0.6 * m_baud / binHz)));
    std::vector<double> band(nb, 0.0);
    for (int k = w; k < nb - w; ++k)
        for (int j = -w; j <= w; ++j)
            band[k] += m_avg[k + j];

    struct Peak
    {
        double hz;
        double db;
    };
    std::vector<Peak> peaks;
    std::vector<double> local;
    const int span = int(250.0 / binHz);
    const int skip = 2 * w + 2;
    for (int k = w + 1; k < nb - w - 1; ++k)
    {
        if (band[k] < band[k - 1] || band[k] < band[k + 1] || band[k] <= 0.0)
            continue;
        local.clear();
        for (int j = std::max(0, k - span); j <= std::min(nb - 1, k + span); ++j)
            if (std::abs(j - k) > skip)
                local.push_back(m_avg[j]);
        if (local.size() < 16)
            continue;
        std::nth_element(local.begin(), local.begin() + long(local.size() / 2), local.end());
        // Median of chi-square(2) power is ln 2 of its mean.
        const double noise = local[local.size() / 2] / std::log(2.0) * double(2 * w + 1);
        if (noise <= 0.0)
            continue;
        const double db = 10.0 * std::log10(band[k] / noise);
        if (db < kPeakDb)
            continue;
        double hz = (k0 + k) * binHz;
        const double a = band[k - 1], b = band[k], c = band[k + 1];
        const double den = a - 2.0 * b + c;
        if (std::fabs(den) > 1e-30)
            hz += std::clamp(0.5 * (a - c) / den, -0.5, 0.5) * binHz;
        peaks.push_back({hz, db});
    }
    // Strongest first; one peak per signal (a text signal's sidebands, IMD
    // products and splatter beside a strong one are not stations).
    std::sort(peaks.begin(), peaks.end(), [](const Peak &p, const Peak &q) { return p.db > q.db; });
    std::vector<Peak> kept;
    for (const Peak &p : peaks)
    {
        bool shadow = false;
        for (const Peak &q : kept)
        {
            const double d = std::fabs(p.hz - q.hz);
            const double weaker = q.db - p.db;
            if (d < 1.6 * m_baud || (d < 4.0 * m_baud && weaker > 15.0))
            {
                shadow = true;
                break;
            }
        }
        if (!shadow)
            kept.push_back(p);
    }

    // Match peaks to channels (by the channel's AFC frequency).
    for (auto &c : m_channels)
        c->hz = c->demod.stats().carrierHz;
    std::vector<bool> used(kept.size(), false);
    for (auto &c : m_channels)
    {
        int best = -1;
        double bestD = 1.0 * m_baud;
        for (size_t i = 0; i < kept.size(); ++i)
        {
            const double d = std::fabs(kept[i].hz - c->hz);
            if (d < bestD)
            {
                bestD = d;
                best = int(i);
            }
        }
        if (best >= 0)
        {
            used[size_t(best)] = true;
            c->silentSec = 0.0;
            // Held peak, falling 1 dB a second (the reading drops with the tail of an over).
            c->peakDb = std::max(kept[size_t(best)].db, c->peakDb - hop);
        }
        else
            c->silentSec += hop;
    }

    // Merge channels that have pulled onto the same signal.
    for (size_t i = 0; i < m_channels.size(); ++i)
        for (size_t j = i + 1; j < m_channels.size();)
        {
            if (std::fabs(m_channels[i]->hz - m_channels[j]->hz) < 0.5 * m_baud)
            {
                if (m_channels[j]->total > m_channels[i]->total)
                    std::swap(m_channels[i], m_channels[j]);
                m_channels.erase(m_channels.begin() + long(j));
                m_changed = true;
            }
            else
                ++j;
        }

    // Drop channels whose signal has gone.
    const size_t before = m_channels.size();
    m_channels.erase(std::remove_if(m_channels.begin(),
                                    m_channels.end(),
                                    [](const std::unique_ptr<Channel> &c)
                                    { return c->silentSec > (c->total > 0 ? kDropSec : kTrialSec); }),
                     m_channels.end());
    if (m_channels.size() != before)
        m_changed = true;

    // New channels for peaks no channel is near.
    for (size_t i = 0; i < kept.size(); ++i)
    {
        if (used[i])
            continue;
        bool near = false;
        for (const auto &c : m_channels)
            if (std::fabs(c->hz - kept[i].hz) < 1.6 * m_baud)
                near = true;
        if (near)
            continue;
        if (int(m_channels.size()) >= kMaxChannels)
        {
            // Make room: one that never printed, else the longest-silent one (> 5 s).
            auto victim = m_channels.end();
            for (auto it = m_channels.begin(); it != m_channels.end(); ++it)
            {
                const bool better = victim == m_channels.end()
                                    || ((*it)->total == 0 && (*victim)->total > 0)
                                    || (((*it)->total == 0) == ((*victim)->total == 0)
                                        && (*it)->silentSec > (*victim)->silentSec);
                if (better)
                    victim = it;
            }
            if (victim == m_channels.end() || ((*victim)->total > 0 && (*victim)->silentSec < 5.0))
                continue;
            m_channels.erase(victim);
        }
        openChannel(kept[i].hz, kept[i].db);
    }
}

std::vector<PskMultiDecoder::ChannelInfo> PskMultiDecoder::channels() const
{
    std::vector<ChannelInfo> v;
    for (const auto &c : m_channels)
    {
        if (c->total == 0)
            continue;
        const PskDemodulator::Stats s = c->demod.stats();
        ChannelInfo ci;
        ci.id = c->id;
        ci.hz = s.carrierHz;
        ci.snrDb = c->peakDb;
        ci.active = c->silentSec < 2.0 && s.squelchOpen;
        ci.text = c->text;
        ci.total = c->total;
        v.push_back(ci);
    }
    std::sort(v.begin(), v.end(), [](const ChannelInfo &a, const ChannelInfo &b) { return a.hz < b.hz; });
    return v;
}
