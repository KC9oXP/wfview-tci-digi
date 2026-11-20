#include "sstvcore.h"

#include <algorithm>
#include <cmath>

namespace sstv
{
namespace
{
// clang-format off
const ModeInfo kModes[ModeCount] = {
    {Martin1,   "Martin M1",  44, Family::Martin,  320, 256,  4.862, 146.432,  446.446, 1},
    {Martin2,   "Martin M2",  40, Family::Martin,  320, 256,  4.862,  73.216,  226.798, 1},
    {Scottie1,  "Scottie S1", 60, Family::Scottie, 320, 256,  9.0,   138.240,  428.220, 1},
    {Scottie2,  "Scottie S2", 56, Family::Scottie, 320, 256,  9.0,    88.064,  277.692, 1},
    {ScottieDX, "Scottie DX", 76, Family::Scottie, 320, 256,  9.0,   345.600, 1050.300, 1},
    {Robot36,   "Robot 36",    8, Family::Robot36, 320, 240,  9.0,    88.000,  150.000, 1},
    {Robot72,   "Robot 72",   12, Family::Robot72, 320, 240,  9.0,   138.000,  300.000, 1},
    {PD50,      "PD50",       93, Family::PD,      320, 256, 20.0,    91.520,  388.160, 2},
    {PD90,      "PD90",       99, Family::PD,      320, 256, 20.0,   170.240,  703.040, 2},
    {PD120,     "PD120",      95, Family::PD,      640, 496, 20.0,   121.600,  508.480, 2},
    {PD160,     "PD160",      98, Family::PD,      512, 400, 20.0,   195.584,  804.416, 2},
    {PD180,     "PD180",      96, Family::PD,      640, 496, 20.0,   183.040,  754.240, 2},
    {PD240,     "PD240",      97, Family::PD,      640, 496, 20.0,   244.480, 1000.000, 2},
};
// clang-format on

constexpr double kMartinSep = 0.572;
constexpr double kScottieSep = 1.5;
constexpr double kRobotPorch = 3.0;
constexpr double kRobotSep = 4.5;
constexpr double kRobotPorch2 = 1.5;
constexpr double kPdPorch = 2.08;
constexpr double kScottieStartSyncMs = 9.0;

// Component start times within a sync period, ms.
struct Layout
{
    double r = 0, g = 0, b = 0; // Martin / Scottie
    double y0 = 0, y1 = 0, cr = 0, cb = 0, c = 0; // Robot / PD
    double sep = 0; // Robot 36 separator (tells R-Y from B-Y)
    double chromaMs = 0;
};

Layout layoutFor(const ModeInfo &m)
{
    Layout l;
    switch (m.family)
    {
    case Family::Martin:
        l.g = m.syncMs + kMartinSep;
        l.b = l.g + m.scanMs + kMartinSep;
        l.r = l.b + m.scanMs + kMartinSep;
        break;
    case Family::Scottie:
        l.g = kScottieSep;
        l.b = l.g + m.scanMs + kScottieSep;
        l.r = l.b + m.scanMs + m.syncMs + kScottieSep;
        break;
    case Family::Robot36:
        l.y0 = m.syncMs + kRobotPorch;
        l.sep = l.y0 + m.scanMs;
        l.c = l.sep + kRobotSep + kRobotPorch2;
        l.chromaMs = m.scanMs / 2.0;
        break;
    case Family::Robot72:
        l.y0 = m.syncMs + kRobotPorch;
        l.cr = l.y0 + m.scanMs + kRobotSep + kRobotPorch2;
        l.chromaMs = m.scanMs / 2.0;
        l.cb = l.cr + l.chromaMs + kRobotSep + kRobotPorch2;
        break;
    case Family::PD:
        l.y0 = m.syncMs + kPdPorch;
        l.cr = l.y0 + m.scanMs;
        l.cb = l.cr + m.scanMs;
        l.y1 = l.cb + m.scanMs;
        l.chromaMs = m.scanMs;
        break;
    }
    return l;
}

inline uint8_t clamp8(double v)
{
    return uint8_t(std::clamp(int(std::lround(v)), 0, 255));
}

inline uint32_t yccToRgb(double y, double cr, double cb)
{
    // QSSTV / MMSSTV scaling
    const double r = y + 1.40 * (cr - 127.5);
    const double b = y + 1.78 * (cb - 127.5);
    const double g = y - 0.71 * (cr - 127.5) - 0.33 * (cb - 127.5);
    return (uint32_t(clamp8(r)) << 16) | (uint32_t(clamp8(g)) << 8) | uint32_t(clamp8(b));
}

inline double hzToValue(double hz)
{
    return (hz - kBlackHz) * 255.0 / (kWhiteHz - kBlackHz);
}

inline float valueToHz(double v)
{
    return float(kBlackHz + std::clamp(v, 0.0, 255.0) * (kWhiteHz - kBlackHz) / 255.0);
}
} // namespace

const ModeInfo &info(int mode)
{
    return kModes[std::clamp(mode, 0, int(ModeCount) - 1)];
}

int modeForVis(int vis)
{
    for (const ModeInfo &m : kModes)
        if (m.vis == vis)
            return m.mode;
    return -1;
}

double imageSeconds(int mode)
{
    const ModeInfo &m = info(mode);
    return m.periods() * m.lineMs / 1000.0 + (m.family == Family::Scottie ? kScottieStartSyncMs / 1000.0 : 0.0);
}

double leanPixels(int mode, double ppm)
{
    const ModeInfo &m = info(mode);
    const double pixelMs = m.scanMs / m.width; // one pixel of one colour (or Y) scan
    return std::fabs(ppm) * 1e-6 * imageSeconds(mode) * 1000.0 / pixelMs;
}

double valueHz(double v)
{
    return valueToHz(v);
}

double syncOffsetMs(int mode)
{
    const ModeInfo &m = info(mode);
    if (m.family == Family::Scottie)
        return 2 * kScottieSep + 2 * m.scanMs;
    return 0.0;
}
} // namespace sstv

using namespace sstv;

// ===========================================================================
// Decoder
// ===========================================================================

namespace
{
constexpr double kBinMs = 5.0;
constexpr double kIdleKeepSec = 8.0; // the sync detector looks back this far
constexpr int kSyncChain = 6; // sync pulses in a row at one mode's period
constexpr double kMaxAutoOffHz = 300.0;
constexpr int kFitEvery = 4;
constexpr int kMaxNoSync = 16;
} // namespace

SstvDecoder::SstvDecoder() = default;

void SstvDecoder::setSampleRate(double fs)
{
    if (fs == m_fs)
        return;
    m_fs = fs;
    m_demod.configure(fs, 1700.0, 1100.0, 12000.0);
    m_rate = m_demod.outRate();
    reset();
}

void SstvDecoder::reset()
{
    m_state = State::Idle;
    m_track.clear();
    m_base = m_now;
    m_bins.clear();
    m_binStarts.clear();
    m_binAcc = 0.0;
    m_binCount = 0;
    m_binStart = m_now;
    m_mode = -1;
    m_img.clear();
    m_syncs.clear();
    m_periodsDone = 0;
    m_pulses.clear();
    m_inPulse = false;
}

int SstvDecoder::width() const
{
    return m_mode < 0 ? 0 : info(m_mode).width;
}

int SstvDecoder::height() const
{
    return m_mode < 0 ? 0 : info(m_mode).height;
}

int SstvDecoder::linesDone() const
{
    if (m_mode < 0)
        return 0;
    return std::min(height(), m_periodsDone * info(m_mode).linesPerPeriod);
}

double SstvDecoder::nominalPeriod() const
{
    return info(m_mode).lineMs * m_rate / 1000.0;
}

double SstvDecoder::periodRatio() const
{
    if (m_mode < 0)
        return 1.0;
    return m_period / nominalPeriod();
}

float SstvDecoder::trackAt(double t) const
{
    const double i = t - m_base;
    if (i <= 0.0)
        return m_track.empty() ? 0.0f : m_track.front();
    const size_t k = size_t(i);
    if (k + 1 >= m_track.size())
        return m_track.empty() ? 0.0f : m_track.back();
    const double f = i - double(k);
    return float(m_track[k] + f * (m_track[k + 1] - m_track[k]));
}

float SstvDecoder::val(double t) const
{
    // Out-of-band readings (noise, no signal) are clipped so they cannot
    // look like sync or white.
    return std::clamp(float(trackAt(t) - offsetHz()), 1000.0f, 2500.0f);
}

float SstvDecoder::pixel(double t, double dur) const
{
    // Mean frequency over [t, t + dur), sampled at about one point per input sample.
    const int n = std::max(1, int(std::ceil(dur)));
    double acc = 0.0;
    for (int i = 0; i < n; ++i)
        acc += val(t + dur * (i + 0.5) / n);
    return float(acc / n);
}

void SstvDecoder::trimTrack()
{
    double keepFrom;
    if (m_state == State::Idle)
        keepFrom = m_now - kIdleKeepSec * m_rate;
    else
        keepFrom = m_t0 - 1.0 * m_rate;
    const double drop = std::floor(keepFrom - m_base);
    if (drop > 4096)
    {
        m_track.erase(m_track.begin(), m_track.begin() + std::ptrdiff_t(drop));
        m_base += drop;
    }
}

SstvDecoder::Events SstvDecoder::process(const float *x, size_t n)
{
    Events ev;
    if (m_fs <= 0.0 || n == 0)
        return ev;
    m_tmp.clear();
    m_demod.process(x, n, m_tmp);
    const int binLen = int(std::lround(kBinMs * m_rate / 1000.0));
    double lvl = 0.0;
    for (const FmDemod::Out &o : m_tmp)
    {
        // Raw track (offset applied when read); wild noise readings clipped.
        const float hz = std::clamp(o.hz, 700.0f, 2900.0f);
        m_track.push_back(hz);
        m_now += 1.0;
        if (m_state == State::Idle && m_autoStart)
            detectPulse(hz, ev);
        lvl += o.level;
        m_binAcc += hz;
        if (++m_binCount >= binLen)
        {
            m_bins.push_back(float(m_binAcc / m_binCount));
            m_binStarts.push_back(m_binStart);
            m_binAcc = 0.0;
            m_binCount = 0;
            m_binStart = m_now;
            if (m_bins.size() > 400)
            {
                m_bins.erase(m_bins.begin(), m_bins.begin() + 200);
                m_binStarts.erase(m_binStarts.begin(), m_binStarts.begin() + 200);
            }
            onBin(ev);
        }
    }
    if (!m_tmp.empty())
    {
        m_level += 0.2 * (lvl / double(m_tmp.size()) - m_level);
        m_levelDb = 20.0 * std::log10(m_level + 1e-9);
        m_lastHz = m_tmp.back().hz;
    }

    if (m_state == State::Receiving)
    {
        const ModeInfo &m = info(m_mode);
        const int periods = m.periods();
        const double margin = (m.syncMs + 12.0) * m_rate / 1000.0;
        while (m_periodsDone < periods && m_now >= m_t0 + (m_periodsDone + 1) * m_period + margin)
        {
            const int p = m_periodsDone;
            measureSync(p);
            renderPeriod(p);
            ++m_periodsDone;
            const int y0 = m.family == Family::Robot36 ? std::max(0, p - 1) : p * m.linesPerPeriod;
            const int y1 = (p + 1) * m.linesPerPeriod;
            ev.dirtyFrom = ev.dirtyFrom < 0 ? y0 : std::min(ev.dirtyFrom, y0);
            ev.dirtyTo = std::max(ev.dirtyTo, y1);
            if (m_autoSlant && m_periodsDone % kFitEvery == 0 && fitTiming())
            {
                for (int q = 0; q < m_periodsDone; ++q)
                    renderPeriod(q);
                ev.redraw = true;
            }
            if (m_noSyncRun >= kMaxNoSync && m_periodsDone > kMaxNoSync)
            {
                // Signal gone: keep what we have.
                m_state = State::Idle;
                ev.finished = true;
                break;
            }
        }
        if (m_state == State::Receiving && m_periodsDone >= periods)
        {
            m_state = State::Idle;
            ev.finished = true;
        }
    }
    trimTrack();
    return ev;
}

void SstvDecoder::onBin(Events &ev)
{
    // VIS: leader 1900 | start 1200 30 ms | 8 bits 30 ms (1100 = 1, 1300 = 0,
    // LSB first, last is even parity) | stop 1200 30 ms. 6 bins per 30 ms.
    const int k = int(m_bins.size()) - 1;
    const int s = k - 59; // first bin of the start bit
    if (s < 40)
        return;
    const auto avg = [this](int a, int b)
    {
        double acc = 0.0;
        for (int i = a; i <= b; ++i)
            acc += m_bins[size_t(i)];
        return acc / (b - a + 1);
    };
    // Offset: the manual one, or (auto) wherever the 1900 Hz leader really is.
    double off = m_manualOff + m_autoOff;
    if (m_autoOffOn)
    {
        std::vector<float> lead(m_bins.begin() + (s - 40), m_bins.begin() + s);
        std::nth_element(lead.begin(), lead.begin() + 20, lead.end());
        const double l = lead[20] - 1900.0;
        if (std::fabs(l - m_manualOff) <= kMaxAutoOffHz)
            off = l;
    }
    if (std::fabs(avg(s + 1, s + 4) - off - 1200.0) > 70.0 || std::fabs(avg(s + 55, s + 58) - off - 1200.0) > 70.0)
        return;
    int leader = 0;
    for (int i = s - 40; i < s; ++i)
        if (std::fabs(m_bins[size_t(i)] - off - 1900.0) < 120.0)
            ++leader;
    if (leader < 32)
        return;
    int code = 0, ones = 0;
    for (int j = 0; j < 8; ++j)
    {
        const double f = avg(s + 6 + 6 * j + 1, s + 6 + 6 * j + 4) - off;
        int bit;
        if (std::fabs(f - 1100.0) < 75.0)
            bit = 1;
        else if (std::fabs(f - 1300.0) < 75.0)
            bit = 0;
        else
            return;
        ones += bit;
        if (j < 7)
            code |= bit << j;
    }
    if (ones % 2 != 0)
        return;
    const int mode = modeForVis(code);
    if (mode < 0)
        return;

    // Refine the timing on the leading edge of the start bit (1900 -> 1200).
    const double guess = m_binStarts[size_t(s)];
    const int half = int(std::lround(0.002 * m_rate));
    double edge = guess;
    double best = 0.0;
    for (double t = guess - 2.0 * half * 2; t <= guess + 2.0 * half * 2; t += 1.0)
    {
        double before = 0.0, after = 0.0;
        for (int i = 1; i <= half; ++i)
        {
            before += trackAt(t - i);
            after += trackAt(t + i - 1);
        }
        const double drop = (before - after) / half;
        if (drop > best)
        {
            best = drop;
            edge = t;
        }
    }
    const double t0 = edge + 300.0 * m_rate / 1000.0;
    if (m_autoOffOn)
    {
        // Refine on the whole leader (40 bins = 200 ms).
        double acc = 0.0;
        int nAcc = 0;
        for (int i = s - 40; i < s; ++i)
            if (std::fabs(m_bins[size_t(i)] - off - 1900.0) < 120.0)
            {
                acc += m_bins[size_t(i)];
                ++nAcc;
            }
        if (nAcc > 0)
            off = acc / nAcc - 1900.0;
        m_autoOff = std::clamp(off - m_manualOff, -kMaxAutoOffHz, kMaxAutoOffHz);
    }
    if (m_state == State::Receiving)
        ev.finished = true; // a new picture interrupts the old one
    m_bins.clear();
    m_binStarts.clear();
    begin(mode, t0, ev);
}

void SstvDecoder::begin(int mode, double t0, Events &ev, bool periodStart)
{
    m_mode = mode;
    const ModeInfo &m = info(mode);
    m_t0 = t0 + (m.family == Family::Scottie && !periodStart ? kScottieStartSyncMs * m_rate / 1000.0 : 0.0);
    m_pulses.clear();
    m_inPulse = false;
    m_period = nominalPeriod() * (1.0 + m_slantPpm * 1e-6);
    m_periodsDone = 0;
    m_noSyncRun = 0;
    m_manual = false;
    m_syncs.clear();
    m_img.assign(size_t(m.width) * size_t(m.height), 0u);
    m_cr.assign(size_t(m.width) * size_t(m.height), 128);
    m_cb.assign(size_t(m.width) * size_t(m.height), 128);
    m_state = State::Receiving;
    ev.started = true;
    ev.redraw = true;
}

void SstvDecoder::detectPulse(float rawHz, Events &ev)
{
    // ~1 ms box average so noise does not split or fake a pulse.
    const size_t len = size_t(std::max(3L, std::lround(0.001 * m_rate)));
    if (m_smooth.size() != len)
    {
        m_smooth.assign(len, 1900.0f);
        m_smoothSum = 1900.0 * double(len);
        m_smoothPos = 0;
    }
    m_smoothSum += rawHz - m_smooth[m_smoothPos];
    m_smooth[m_smoothPos] = rawHz;
    m_smoothPos = (m_smoothPos + 1) % len;
    const double hz = m_smoothSum / double(len) - offsetHz();
    const double now = m_now - double(len) / 2.0; // centre of the box
    if (hz < 1350.0)
    {
        if (!m_inPulse)
        {
            m_inPulse = true;
            m_pulseStart = now;
            m_pulseHz = 0.0;
            m_pulseN = 0;
        }
        m_pulseHz += rawHz;
        ++m_pulseN;
        return;
    }
    if (!m_inPulse)
        return;
    m_inPulse = false;
    const double width = now - m_pulseStart;
    const double ms = width * 1000.0 / m_rate;
    if (ms < 3.0 || ms > 26.0 || m_pulseN == 0)
        return; // not a sync pulse (VIS bits and breaks are 30 ms)
    const double meanHz = m_pulseHz / m_pulseN;
    m_pulses.push_back({m_pulseStart, width, meanHz});
    if (m_pulses.size() > 40)
        m_pulses.erase(m_pulses.begin(), m_pulses.begin() + 20);
    // Forget pulses older than the track kept in Idle.
    const double keep = m_now - (kIdleKeepSec - 0.5) * m_rate;
    while (!m_pulses.empty() && m_pulses.front().t < keep)
        m_pulses.erase(m_pulses.begin());
    trySyncStart(ev);
}

void SstvDecoder::trySyncStart(Events &ev)
{
    if (int(m_pulses.size()) < kSyncChain)
        return;
    int bestMode = -1;
    double bestFirst = 0.0, bestResid = 1e9, bestHz = 0.0;
    int bestChain = 0;
    for (int mode = 0; mode < ModeCount; ++mode)
    {
        const ModeInfo &m = info(mode);
        const double P = m.lineMs * m_rate / 1000.0 * (1.0 + m_slantPpm * 1e-6);
        const double W = m.syncMs * m_rate / 1000.0;
        const double tol = 1.5 * m_rate / 1000.0;
        const Pulse &last = m_pulses.back();
        if (std::fabs(last.width - W) > std::max(2.5 * m_rate / 1000.0, 0.45 * W))
            continue;
        // Walk back from the newest pulse along whole periods (a missed sync is allowed).
        double cur = last.t;
        int chain = 1, singles = 0;
        double resid = 0.0, first = last.t, hzSum = last.hz;
        for (int i = int(m_pulses.size()) - 2; i >= 0; --i)
        {
            const Pulse &q = m_pulses[size_t(i)];
            const double d = cur - q.t;
            if (d > 2.5 * P)
                break;
            const long k = std::lround(d / P);
            if (k < 1 || k > 2 || std::fabs(d - double(k) * P) > tol * double(k)
                || std::fabs(q.width - W) > std::max(2.5 * m_rate / 1000.0, 0.45 * W))
                continue; // a stray dip: skip it
            resid += std::fabs(d - double(k) * P);
            if (k == 1)
                ++singles;
            ++chain;
            hzSum += q.hz;
            cur = q.t;
            first = q.t;
        }
        // Only syncs dip to 1200 Hz in SSTV: other dips between them (another
        // digital mode, PSK phase flips) mean this is not a picture.
        int strays = 0;
        for (const Pulse &q : m_pulses)
            if (q.t > first + tol && q.t < last.t - tol)
            {
                bool inChain = false;
                const long k = std::lround((last.t - q.t) / P);
                inChain = std::fabs((last.t - q.t) - double(k) * P) <= tol * double(std::max(1L, k));
                if (!inChain)
                    ++strays;
            }
        double meanHz = hzSum / chain - offsetHz();
        if (chain >= kSyncChain && singles >= kSyncChain - 2 && strays <= chain / 3 && meanHz < 1290.0)
        {
            const double r = resid / chain;
            if (chain > bestChain || (chain == bestChain && r < bestResid))
            {
                bestMode = mode;
                bestChain = chain;
                bestResid = r;
                bestFirst = first;
                bestHz = hzSum / chain;
            }
        }
    }
    if (bestMode < 0)
        return;
    if (m_autoOffOn)
        m_autoOff = std::clamp(bestHz - 1200.0 - m_manualOff, -kMaxAutoOffHz, kMaxAutoOffHz);
    // The first pulse of the chain is the start of a sync; the picture starts
    // there (line 0 = the first line seen).
    const double t0 = bestFirst - syncOffsetMs(bestMode) * m_rate / 1000.0;
    m_bins.clear();
    m_binStarts.clear();
    begin(bestMode, t0, ev, true);
    ev.noVis = true;
}

void SstvDecoder::startManual(int mode)
{
    Events ev;
    begin(std::clamp(mode, 0, int(ModeCount) - 1), m_now, ev);
    m_manual = true;
}

void SstvDecoder::stop()
{
    m_state = State::Idle;
}

void SstvDecoder::measureSync(int p)
{
    const ModeInfo &m = info(m_mode);
    const double k = m_period / nominalPeriod();
    const double L = m.syncMs * m_rate / 1000.0 * k;
    const double expect = m_t0 + p * m_period + syncOffsetMs(m_mode) * m_rate / 1000.0 * k;
    // Before the first timing fit of a manual start the phase is unknown.
    const bool wide = m_manual && m_syncs.empty() && p < 8;
    const double w = wide ? m_period / 2.0 : 10.0 * m_rate / 1000.0;
    const long a = long(std::floor(expect - w - L));
    const long b = long(std::ceil(expect + w + 2 * L));
    std::vector<int> pre(size_t(b - a + 2), 0);
    for (long t = a; t <= b; ++t)
        pre[size_t(t - a + 1)] = pre[size_t(t - a)] + (val(double(t)) < 1350.0f ? 1 : 0);
    const auto count = [&](long from, long to) // [from, to)
    {
        from = std::clamp(from, a, b + 1);
        to = std::clamp(to, a, b + 1);
        return to > from ? pre[size_t(to - a)] - pre[size_t(from - a)] : 0;
    };
    const long Li = std::max(2L, long(std::lround(L)));
    double bestScore = -1e9;
    long bestT = 0;
    double bestIn = 0.0;
    for (long t = long(std::lround(expect - w)); t <= long(std::lround(expect + w)); ++t)
    {
        const double in = double(count(t, t + Li)) / Li;
        const double out = double(count(t - Li / 2, t) + count(t + Li, t + Li + Li / 2)) / Li;
        const double score = in - out;
        if (score > bestScore)
        {
            bestScore = score;
            bestT = t;
            bestIn = in;
        }
    }
    if (bestIn > 0.65 && bestScore > 0.4)
    {
        if (wide)
        {
            // First sync of a manual start: move the timing onto it.
            m_t0 += double(bestT) - expect;
        }
        m_syncs.emplace_back(p, double(bestT));
        m_noSyncRun = 0;
        if (m_autoOffOn)
        {
            // The middle half of the pulse is 1200 Hz + offset.
            double acc = 0.0;
            int nAcc = 0;
            for (long t = bestT + Li / 4; t < bestT + 3 * Li / 4; ++t, ++nAcc)
                acc += trackAt(double(t));
            if (nAcc > 0)
            {
                const double meas = acc / nAcc - 1200.0 - m_manualOff;
                if (std::fabs(meas - m_autoOff) < 150.0)
                    m_autoOff = std::clamp(m_autoOff + 0.15 * (meas - m_autoOff), -kMaxAutoOffHz, kMaxAutoOffHz);
            }
        }
    }
    else
    {
        ++m_noSyncRun;
    }
}

bool SstvDecoder::fitTiming()
{
    if (m_syncs.size() < 6)
        return false;
    const double nominal = nominalPeriod();
    std::vector<std::pair<int, double>> pts = m_syncs;
    double A = 0.0, B = m_period;
    for (int iter = 0; iter < 4; ++iter)
    {
        double sp = 0, st = 0, spp = 0, spt = 0;
        const double n = double(pts.size());
        if (n < 6)
            return false;
        for (const auto &q : pts)
        {
            sp += q.first;
            st += q.second;
            spp += double(q.first) * q.first;
            spt += double(q.first) * q.second;
        }
        const double den = n * spp - sp * sp;
        if (den <= 0.0)
            return false;
        B = (n * spt - sp * st) / den;
        A = (st - B * sp) / n;
        // drop points more than 2.5 ms off the line
        const double lim = 2.5 * m_rate / 1000.0;
        std::vector<std::pair<int, double>> keep;
        for (const auto &q : pts)
            if (std::fabs(q.second - (A + B * q.first)) < lim)
                keep.push_back(q);
        if (keep.size() == pts.size())
            break;
        pts.swap(keep);
    }
    if (pts.back().first - pts.front().first < 5)
        return false;
    if (std::fabs(B / nominal - 1.0) > 0.01)
        return false; // more than 1 % off: not a believable clock error
    const double k = B / nominal;
    const double newT0 = A - syncOffsetMs(m_mode) * m_rate / 1000.0 * k;
    const int last = std::max(1, m_periodsDone);
    const double moved = std::fabs((newT0 + B * last) - (m_t0 + m_period * last));
    const bool changed = std::fabs(newT0 - m_t0) > 0.3 || moved > 0.3;
    m_t0 = newT0;
    m_period = B;
    return changed;
}

void SstvDecoder::renderPeriod(int p)
{
    const ModeInfo &m = info(m_mode);
    const Layout l = layoutFor(m);
    const double k = m_period / nominalPeriod();
    const double msToS = m_rate / 1000.0 * k;
    const double base = m_t0 + p * m_period;
    const int W = m.width;
    const auto comp = [&](double startMs, double durMs, int i)
    {
        const double px = durMs / W * msToS;
        return hzToValue(pixel(base + startMs * msToS + i * px, px));
    };

    switch (m.family)
    {
    case Family::Martin:
    case Family::Scottie:
    {
        uint32_t *row = &m_img[size_t(p) * size_t(W)];
        for (int i = 0; i < W; ++i)
        {
            const uint8_t r = clamp8(comp(l.r, m.scanMs, i));
            const uint8_t g = clamp8(comp(l.g, m.scanMs, i));
            const uint8_t b = clamp8(comp(l.b, m.scanMs, i));
            row[i] = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
        }
        break;
    }
    case Family::Robot72:
    {
        uint32_t *row = &m_img[size_t(p) * size_t(W)];
        for (int i = 0; i < W; ++i)
            row[i] = yccToRgb(comp(l.y0, m.scanMs, i), comp(l.cr, l.chromaMs, i), comp(l.cb, l.chromaMs, i));
        break;
    }
    case Family::PD:
    {
        uint32_t *r0 = &m_img[size_t(2 * p) * size_t(W)];
        uint32_t *r1 = &m_img[size_t(2 * p + 1) * size_t(W)];
        for (int i = 0; i < W; ++i)
        {
            const double cr = comp(l.cr, l.chromaMs, i);
            const double cb = comp(l.cb, l.chromaMs, i);
            r0[i] = yccToRgb(comp(l.y0, m.scanMs, i), cr, cb);
            r1[i] = yccToRgb(comp(l.y1, m.scanMs, i), cr, cb);
        }
        break;
    }
    case Family::Robot36:
    {
        // The separator says which chroma this line carries: 1500 Hz R-Y, 2300 Hz B-Y.
        const double sepHz = pixel(base + (l.sep + 0.5) * msToS, (kRobotSep - 1.0) * msToS);
        const bool isCb = sepHz > 1900.0;
        const size_t off = size_t(p) * size_t(W);
        std::vector<uint8_t> y(static_cast<size_t>(W));
        for (int i = 0; i < W; ++i)
        {
            y[size_t(i)] = clamp8(comp(l.y0, m.scanMs, i));
            const uint8_t c = clamp8(comp(l.c, l.chromaMs, i));
            if (isCb)
            {
                m_cb[off + size_t(i)] = c;
                if (p > 0)
                    m_cb[off - size_t(W) + size_t(i)] = c; // the line above shares it
            }
            else
            {
                m_cr[off + size_t(i)] = c;
                if (p + 1 < m.height)
                    m_cr[off + size_t(W) + size_t(i)] = c;
            }
        }
        // Luma kept in the alpha-free low byte of a separate pass: store via m_img then convert.
        for (int i = 0; i < W; ++i)
            m_img[off + size_t(i)]
                = yccToRgb(y[size_t(i)], m_cr[off + size_t(i)], m_cb[off + size_t(i)]) | (uint32_t(y[size_t(i)]) << 24);
        if (p > 0 && isCb)
        {
            // Redo the line above with its now-known B-Y.
            const size_t up = off - size_t(W);
            for (int i = 0; i < W; ++i)
            {
                const uint8_t yy = uint8_t(m_img[up + size_t(i)] >> 24);
                m_img[up + size_t(i)] = yccToRgb(yy, m_cr[up + size_t(i)], m_cb[up + size_t(i)]) | (uint32_t(yy) << 24);
            }
        }
        break;
    }
    }
}

// ===========================================================================
// Encoder
// ===========================================================================

void SstvEncoder::clear()
{
    m_mode = -1;
    m_rgb.clear();
    m_segs.clear();
    m_segIdx = 0;
    m_stage = 0;
    m_segEnd = 0.0;
    m_t = 0.0;
    m_phase = 0.0;
    m_done = false;
    m_total = 0.0;
}

void SstvEncoder::start(int mode, const std::vector<uint32_t> &rgb)
{
    clear();
    m_mode = std::clamp(mode, 0, int(ModeCount) - 1);
    const ModeInfo &m = info(m_mode);
    m_rgb = rgb;
    m_rgb.resize(size_t(m.width) * size_t(m.height), 0u);
    m_total = 0.910 + imageSeconds(m_mode); // header: 300 + 10 + 300 + 10 * 30 ms
}

double SstvEncoder::progress() const
{
    if (m_total <= 0.0)
        return 0.0;
    return std::clamp(m_t / m_total, 0.0, 1.0);
}

int SstvEncoder::lineSending() const
{
    if (m_mode < 0)
        return -1;
    const ModeInfo &m = info(m_mode);
    if (m_done)
        return m.height;
    // nextSegments() bumps m_stage as it builds a period, so period m_stage - 2 is on the air.
    if (m_stage < 2)
        return -1;
    return std::clamp((m_stage - 2) * m.linesPerPeriod, 0, m.height);
}

void SstvEncoder::lineYcc(int y, std::vector<float> &yv, std::vector<float> &cr, std::vector<float> &cb) const
{
    const ModeInfo &m = info(m_mode);
    const int W = m.width;
    yv.resize(static_cast<size_t>(W));
    cr.resize(static_cast<size_t>(W));
    cb.resize(static_cast<size_t>(W));
    y = std::clamp(y, 0, m.height - 1);
    for (int i = 0; i < W; ++i)
    {
        const uint32_t c = m_rgb[size_t(y) * size_t(W) + size_t(i)];
        const double r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
        const double Y = 0.30 * r + 0.59 * g + 0.11 * b;
        yv[size_t(i)] = float(Y);
        cr[size_t(i)] = float(std::clamp((r - Y) / 1.40 + 127.5, 0.0, 255.0));
        cb[size_t(i)] = float(std::clamp((b - Y) / 1.78 + 127.5, 0.0, 255.0));
    }
}

void SstvEncoder::nextSegments()
{
    m_segs.clear();
    m_segIdx = 0;
    if (m_mode < 0)
    {
        m_done = true;
        return;
    }
    const ModeInfo &m = info(m_mode);
    const int W = m.width;
    const auto tone = [this](double hz, double ms) { m_segs.push_back({float(hz), ms}); };
    const auto scan = [&](const float *v, double ms)
    {
        for (int i = 0; i < W; ++i)
            m_segs.push_back({valueToHz(v[i]), ms / W});
    };

    if (m_stage == 0)
    {
        tone(1900, 300);
        tone(1200, 10);
        tone(1900, 300);
        tone(1200, 30);
        int ones = 0;
        for (int j = 0; j < 7; ++j)
        {
            const int bit = (m.vis >> j) & 1;
            ones += bit;
            tone(bit ? 1100 : 1300, 30);
        }
        tone((ones & 1) ? 1100 : 1300, 30); // even parity
        tone(1200, 30);
        if (m.family == Family::Scottie)
            tone(kSyncHz, 9.0);
        ++m_stage;
        return;
    }
    const int p = m_stage - 1;
    if (p >= m.periods())
    {
        m_done = true;
        return;
    }
    ++m_stage;

    std::vector<float> c0(static_cast<size_t>(W)), c1(static_cast<size_t>(W)), c2(static_cast<size_t>(W));
    std::vector<float> y1, cr1, cb1;
    switch (m.family)
    {
    case Family::Martin:
    case Family::Scottie:
    {
        for (int i = 0; i < W; ++i)
        {
            const uint32_t c = m_rgb[size_t(p) * size_t(W) + size_t(i)];
            c0[size_t(i)] = float((c >> 16) & 0xff);
            c1[size_t(i)] = float((c >> 8) & 0xff);
            c2[size_t(i)] = float(c & 0xff);
        }
        if (m.family == Family::Martin)
        {
            tone(kSyncHz, m.syncMs);
            tone(kBlackHz, kMartinSep);
            scan(c1.data(), m.scanMs);
            tone(kBlackHz, kMartinSep);
            scan(c2.data(), m.scanMs);
            tone(kBlackHz, kMartinSep);
            scan(c0.data(), m.scanMs);
            tone(kBlackHz, kMartinSep);
        }
        else
        {
            tone(kBlackHz, kScottieSep);
            scan(c1.data(), m.scanMs);
            tone(kBlackHz, kScottieSep);
            scan(c2.data(), m.scanMs);
            tone(kSyncHz, m.syncMs);
            tone(kBlackHz, kScottieSep);
            scan(c0.data(), m.scanMs);
        }
        break;
    }
    case Family::Robot72:
    {
        lineYcc(p, c0, c1, c2);
        tone(kSyncHz, m.syncMs);
        tone(kBlackHz, kRobotPorch);
        scan(c0.data(), m.scanMs);
        tone(1500, kRobotSep);
        tone(1900, kRobotPorch2);
        scan(c1.data(), m.scanMs / 2);
        tone(2300, kRobotSep);
        tone(1900, kRobotPorch2);
        scan(c2.data(), m.scanMs / 2);
        break;
    }
    case Family::Robot36:
    {
        const bool odd = p & 1;
        lineYcc(p, c0, c1, c2);
        lineYcc(odd ? p - 1 : p + 1, y1, cr1, cb1);
        std::vector<float> c(static_cast<size_t>(W));
        for (int i = 0; i < W; ++i)
            c[size_t(i)] = odd ? 0.5f * (c2[size_t(i)] + cb1[size_t(i)]) : 0.5f * (c1[size_t(i)] + cr1[size_t(i)]);
        tone(kSyncHz, m.syncMs);
        tone(kBlackHz, kRobotPorch);
        scan(c0.data(), m.scanMs);
        tone(odd ? 2300 : 1500, kRobotSep);
        tone(1900, kRobotPorch2);
        scan(c.data(), m.scanMs / 2);
        break;
    }
    case Family::PD:
    {
        lineYcc(2 * p, c0, c1, c2);
        lineYcc(2 * p + 1, y1, cr1, cb1);
        for (int i = 0; i < W; ++i)
        {
            c1[size_t(i)] = 0.5f * (c1[size_t(i)] + cr1[size_t(i)]);
            c2[size_t(i)] = 0.5f * (c2[size_t(i)] + cb1[size_t(i)]);
        }
        tone(kSyncHz, m.syncMs);
        tone(kBlackHz, kPdPorch);
        scan(c0.data(), m.scanMs);
        scan(c1.data(), m.scanMs);
        scan(c2.data(), m.scanMs);
        scan(y1.data(), m.scanMs);
        break;
    }
    }
}

void SstvEncoder::render(float *out, size_t n, double fs)
{
    // A fast output clock plays the samples early; stretch the timing (not the tones) to match.
    const double dt = 1.0 / (fs * (1.0 + m_timingPpm * 1e-6));
    const double toneDt = 1.0 / fs;
    for (size_t i = 0; i < n; ++i)
    {
        if (m_done || m_mode < 0)
        {
            out[i] = 0.0f;
            continue;
        }
        while (m_t >= m_segEnd && !m_done)
        {
            if (m_segIdx + 1 < m_segs.size())
                ++m_segIdx;
            else
            {
                nextSegments();
                if (m_done)
                    break;
            }
            m_segEnd += m_segs[m_segIdx].ms / 1000.0;
        }
        if (m_done)
        {
            out[i] = 0.0f;
            continue;
        }
        m_phase += 2.0 * M_PI * (m_segs[m_segIdx].hz + m_offset) * toneDt;
        if (m_phase > 2.0 * M_PI)
            m_phase -= 2.0 * M_PI;
        out[i] = float(std::sin(m_phase));
        m_t += dt;
    }
}
