#include "wefaxcore.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kWindowSec = 0.25;
constexpr double kPulseFrac = 0.05; // phasing pulse, fraction of a line
} // namespace

// ===========================================================================
// Decoder
// ===========================================================================

void WefaxDecoder::setSampleRate(double fs)
{
    if (fs == m_fs)
        return;
    m_fs = fs;
    m_demod.configure(fs, 1900.0, 1400.0, 12000.0);
    m_rate = m_demod.outRate();
    reset();
}

void WefaxDecoder::setLpm(double lpm)
{
    m_lpm = std::clamp(lpm, 30.0, 300.0);
}

void WefaxDecoder::setIoc(int ioc)
{
    m_ioc = ioc == 288 ? 288 : 576;
}

void WefaxDecoder::reset()
{
    m_state = State::Idle;
    m_grey.clear();
    m_base = m_now;
    m_img.clear();
    m_centres.clear();
    m_phasingLines = 0;
    m_startRun = m_notStartRun = m_stopRun = 0;
    m_aptCount = m_aptTransitions = 0;
    m_periodRatio = 1.0;
}

double WefaxDecoder::nominalPeriod() const
{
    return m_rate * 60.0 / m_lpm * (1.0 + m_slantPpm * 1e-6);
}

float WefaxDecoder::at(double t) const
{
    const double i = t - m_base;
    if (m_grey.empty())
        return 0.0f;
    if (i <= 0.0)
        return m_grey.front();
    const size_t k = size_t(i);
    if (k + 1 >= m_grey.size())
        return m_grey.back();
    const double f = i - double(k);
    return float(m_grey[k] + f * (m_grey[k + 1] - m_grey[k]));
}

void WefaxDecoder::setState(State s, Events &ev)
{
    if (s == m_state)
        return;
    m_state = s;
    ev.stateChanged = true;
}

void WefaxDecoder::startManual()
{
    Events ev;
    m_img.clear();
    m_period = nominalPeriod();
    m_periodRatio = 1.0;
    m_lineStart = m_now;
    m_stopRun = 0;
    setState(State::Image, ev);
}

void WefaxDecoder::stop()
{
    Events ev;
    setState(State::Idle, ev);
}

WefaxDecoder::Events WefaxDecoder::process(const float *x, size_t n)
{
    Events ev;
    if (m_fs <= 0.0 || n == 0)
        return ev;
    m_tmp.clear();
    m_demod.process(x, n, m_tmp);
    const int window = int(std::lround(kWindowSec * m_rate));
    double lvl = 0.0;
    for (const FmDemod::Out &o : m_tmp)
    {
        const float g = std::clamp((o.hz - 1500.0f) / 800.0f, 0.0f, 1.0f);
        m_grey.push_back(g);
        m_now += 1.0;
        lvl += o.level;
        if (!m_aptHigh && g > 0.65f)
        {
            m_aptHigh = true;
            ++m_aptTransitions;
        }
        else if (m_aptHigh && g < 0.35f)
        {
            m_aptHigh = false;
            ++m_aptTransitions;
        }
        m_aptBuf.push_back(g);
        if (++m_aptCount >= window)
        {
            aptWindow(ev);
            m_aptCount = 0;
            m_aptTransitions = 0;
            m_aptBuf.clear();
        }
        if (m_state == State::Phasing && m_now >= m_phStart + std::round(nominalPeriod()) + 1.0)
            analysePhasing(ev);
    }
    if (!m_tmp.empty())
    {
        m_level += 0.2 * (lvl / double(m_tmp.size()) - m_level);
        m_levelDb = 20.0 * std::log10(m_level + 1e-9);
    }
    if (m_state == State::Image)
        emitRows(ev);

    // Keep the grey track from where the next line or window starts.
    double keepFrom = m_now - 2.0 * m_rate;
    if (m_state == State::Phasing)
        keepFrom = std::min(keepFrom, m_phStart - m_rate);
    else if (m_state == State::Image)
        keepFrom = std::min(keepFrom, m_lineStart - m_rate);
    const double drop = std::floor(keepFrom - m_base);
    if (drop > 8192)
    {
        m_grey.erase(m_grey.begin(), m_grey.begin() + std::ptrdiff_t(drop));
        m_base += drop;
    }
    return ev;
}

double WefaxDecoder::toneShare(double hz) const
{
    // Share of the grey signal's AC power at hz (Goertzel). A clean
    // black/white square wave puts about 80 % in its fundamental; noise a few %.
    const size_t n = m_aptBuf.size();
    if (n < 16)
        return 0.0;
    double mean = 0.0;
    for (float v : m_aptBuf)
        mean += v;
    mean /= double(n);
    double var = 0.0, s1 = 0.0, s2 = 0.0;
    const double c = 2.0 * std::cos(2.0 * M_PI * hz / m_rate);
    for (float v : m_aptBuf)
    {
        const double x = v - mean;
        var += x * x;
        const double s0 = x + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2; // |X|^2
    if (var <= 1e-9)
        return 0.0;
    return 2.0 * p / (double(n) * var);
}

void WefaxDecoder::aptWindow(Events &ev)
{
    m_aptHz = m_aptTransitions / 2.0 / kWindowSec;
    const bool start576 = std::fabs(m_aptHz - 300.0) < 40.0 && toneShare(300.0) > 0.4;
    const bool start288 = std::fabs(m_aptHz - 675.0) < 60.0 && toneShare(675.0) > 0.4;
    const bool stopTone = std::fabs(m_aptHz - 450.0) < 45.0 && toneShare(450.0) > 0.4;
    if (!m_auto)
        return;

    if (m_state == State::Idle || m_state == State::Image)
    {
        m_startRun = (start576 || start288) ? m_startRun + 1 : 0;
        if (m_startRun >= 3)
        {
            if (m_state == State::Image)
                ev.finished = true;
            m_ioc = start288 ? 288 : 576;
            m_img.clear();
            m_notStartRun = 0;
            setState(State::Start, ev);
            return;
        }
    }
    if (m_state == State::Start)
    {
        m_notStartRun = (start576 || start288) ? 0 : m_notStartRun + 1;
        if (start576 || start288)
            m_ioc = start288 ? 288 : 576;
        if (m_notStartRun >= 2)
        {
            m_centres.clear();
            m_phasingLines = 0;
            m_nonPhasingRun = 0;
            // The tone ended up to two windows ago; phasing starts there.
            m_phStart = m_now - 2.0 * kWindowSec * m_rate;
            m_phEnteredAt = m_now;
            setState(State::Phasing, ev);
        }
        return;
    }
    if (m_state == State::Image || m_state == State::Phasing)
    {
        m_stopRun = stopTone ? m_stopRun + 1 : 0;
        if (m_stopRun >= 4)
        {
            m_stopRun = 0;
            ev.finished = m_state == State::Image;
            setState(State::Idle, ev);
        }
    }
}

void WefaxDecoder::analysePhasing(Events &ev)
{
    const int N = int(std::round(nominalPeriod()));
    const int w = std::max(4, int(kPulseFrac * N));
    std::vector<float> v(static_cast<size_t>(N));
    double mean = 0.0;
    for (int i = 0; i < N; ++i)
    {
        v[size_t(i)] = at(m_phStart + i);
        mean += v[size_t(i)];
    }
    mean /= N;
    // Circular moving average: a pulse cut by the window edges joins up.
    double acc = 0.0;
    for (int i = 0; i < w; ++i)
        acc += v[size_t(i)];
    double best = acc;
    int bestI = 0;
    for (int i = 1; i < N; ++i)
    {
        acc += v[size_t((i + w - 1) % N)] - v[size_t(i - 1)];
        if (acc > best)
        {
            best = acc;
            bestI = i;
        }
    }
    best /= w;
    if (best > 0.7 && mean < 0.25)
    {
        const double centre = m_phStart + std::fmod(bestI + w / 2.0, double(N));
        m_centres.push_back(centre);
        ++m_phasingLines;
        m_nonPhasingRun = 0;
    }
    else if (m_phasingLines > 0)
    {
        ++m_nonPhasingRun;
    }
    m_phStart += N;

    const bool enough = m_phasingLines >= 4 && m_nonPhasingRun >= 1;
    const bool tooLong = m_phasingLines >= 60 || m_now - m_phEnteredAt > 45.0 * m_rate;
    if (!enough && !tooLong)
        return;

    m_period = nominalPeriod();
    m_periodRatio = 1.0;
    m_lineStart = m_phStart - N; // the window just analysed is the first image line
    if (m_centres.size() >= 3)
    {
        // Straight-line fit of the pulse centres against their line number.
        const double c0 = m_centres.front();
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (double c : m_centres)
        {
            const double k = std::round((c - c0) / m_period);
            sx += k;
            sy += c;
            sxx += k * k;
            sxy += k * c;
        }
        const double n = double(m_centres.size());
        const double den = n * sxx - sx * sx;
        if (den > 0.0)
        {
            const double B = (n * sxy - sx * sy) / den;
            const double A = (sy - B * sx) / n;
            if (std::fabs(B / m_period - 1.0) < 0.01)
            {
                m_periodRatio = B / (m_rate * 60.0 / m_lpm);
                m_period = B;
            }
            // First line boundary (pulse centre) at or after the image start.
            const double k = std::ceil((m_lineStart - A) / m_period - 0.5);
            m_lineStart = A + k * m_period;
        }
    }
    m_img.clear();
    m_stopRun = 0;
    setState(State::Image, ev);
    ev.started = true;
}

void WefaxDecoder::emitRows(Events &ev)
{
    const int W = width();
    while (m_now >= m_lineStart + m_period + 2.0)
    {
        const double px = m_period / W;
        const int sub = std::max(1, int(std::ceil(px)));
        for (int j = 0; j < W; ++j)
        {
            double acc = 0.0;
            for (int s = 0; s < sub; ++s)
                acc += at(m_lineStart + (j + (s + 0.5) / sub) * px);
            m_img.push_back(uint8_t(std::clamp(int(std::lround(acc / sub * 255.0)), 0, 255)));
        }
        m_lineStart += m_period;
        ++ev.rowsAdded;
        if (rows() >= kMaxRows)
        {
            ev.finished = true;
            setState(State::Idle, ev);
            return;
        }
    }
}

// ===========================================================================
// Encoder (tests)
// ===========================================================================

void WefaxEncoder::start(const std::vector<uint8_t> &grey, int width, double lpm, int ioc, int phasingLines)
{
    m_grey = grey;
    m_w = std::max(1, width);
    m_rows = int(grey.size() / size_t(m_w));
    m_lpm = lpm;
    m_ioc = ioc;
    m_phasing = phasingLines;
    m_t = 0.0;
    m_phase = 0.0;
    const double P = 60.0 / m_lpm;
    m_total = 5.0 + m_phasing * P + m_rows * P + 5.0 + 2.0;
    m_done = false;
}

double WefaxEncoder::levelAt(double t) const
{
    const double P = 60.0 / m_lpm;
    if (t < 5.0)
    {
        const double f = m_ioc == 288 ? 675.0 : 300.0;
        return std::fmod(t * f, 1.0) < 0.5 ? 1.0 : 0.0;
    }
    t -= 5.0;
    if (t < m_phasing * P)
    {
        const double u = std::fmod(t, P) / P;
        return (u < kPulseFrac / 2 || u > 1.0 - kPulseFrac / 2) ? 1.0 : 0.0;
    }
    t -= m_phasing * P;
    if (t < m_rows * P)
    {
        const int r = std::min(m_rows - 1, int(t / P));
        const int c = std::min(m_w - 1, int(std::fmod(t, P) / P * m_w));
        return m_grey[size_t(r) * size_t(m_w) + size_t(c)] / 255.0;
    }
    t -= m_rows * P;
    if (t < 5.0)
        return std::fmod(t * 450.0, 1.0) < 0.5 ? 1.0 : 0.0;
    return 0.0;
}

void WefaxEncoder::render(float *out, size_t n, double fs)
{
    for (size_t i = 0; i < n; ++i)
    {
        if (m_done)
        {
            out[i] = 0.0f;
            continue;
        }
        const double hz = 1500.0 + 800.0 * levelAt(m_t);
        m_phase += 2.0 * M_PI * hz / fs;
        if (m_phase > 2.0 * M_PI)
            m_phase -= 2.0 * M_PI;
        out[i] = float(std::sin(m_phase));
        m_t += 1.0 / fs;
        if (m_t >= m_total)
            m_done = true;
    }
}
