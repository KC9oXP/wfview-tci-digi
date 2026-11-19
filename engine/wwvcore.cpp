#include "wwvcore.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
constexpr double kTickBoxSec = 0.005;
constexpr double kCodeBoxSec = 0.050; // nulls at 20 Hz steps: DC (seen at 100 Hz) and 60 Hz hum
constexpr double kRingSec = 3.0;
constexpr int kMarkers[6] = {9, 19, 29, 39, 49, 59};
// Seconds that are always 0 in the code.
constexpr int kUnused[] = {1, 8, 14, 18, 24, 27, 28, 34, 42, 43, 44, 45, 46, 47, 48};

bool isLeap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

void putBcd(std::string &s, int value, const int *secs, const int *weights, int n)
{
    // weights descending is not needed: greedy from the largest
    for (int i = n - 1; i >= 0; --i)
        if (value >= weights[i])
        {
            s[size_t(secs[i])] = '1';
            value -= weights[i];
        }
}

int getBcd(const std::string &s, const int *secs, const int *weights, int n)
{
    int v = 0;
    for (int i = 0; i < n; ++i)
        if (s[size_t(secs[i])] == '1')
            v += weights[i];
    return v;
}

const int kMinSecs[] = {10, 11, 12, 13, 15, 16, 17}, kMinW[] = {1, 2, 4, 8, 10, 20, 40};
const int kHourSecs[] = {20, 21, 22, 23, 25, 26}, kHourW[] = {1, 2, 4, 8, 10, 20};
const int kDaySecs[] = {30, 31, 32, 33, 35, 36, 37, 38, 40, 41}, kDayW[] = {1, 2, 4, 8, 10, 20, 40, 80, 100, 200};
const int kYearSecs[] = {4, 5, 6, 7, 51, 52, 53, 54}, kYearW[] = {1, 2, 4, 8, 10, 20, 40, 80};
const int kDutSecs[] = {56, 57, 58}, kDutW[] = {1, 2, 4};

bool bcdDigitsOk(const std::string &s, const int *secs, int n)
{
    // each group of four units bits must be 0..9
    int v = 0;
    for (int i = 0; i < n && i < 4; ++i)
        if (s[size_t(secs[i])] == '1')
            v += 1 << i;
    return v <= 9;
}
} // namespace

namespace wwv
{
long Time::minutesSince2000() const
{
    long days = 0;
    for (int y = 2000; y < year; ++y)
        days += isLeap(y) ? 366 : 365;
    days += dayOfYear - 1;
    return (days * 24 + hour) * 60 + minute;
}

std::string encodeFrame(const Time &t)
{
    std::string s(60, '0');
    s[0] = '-';
    for (int m : kMarkers)
        s[size_t(m)] = 'M';
    putBcd(s, t.minute, kMinSecs, kMinW, 7);
    putBcd(s, t.hour, kHourSecs, kHourW, 6);
    putBcd(s, t.dayOfYear, kDaySecs, kDayW, 10);
    putBcd(s, t.year % 100, kYearSecs, kYearW, 8);
    if (t.dst2)
        s[2] = '1';
    if (t.leapWarning)
        s[3] = '1';
    if (t.dut1 >= 0.0)
        s[50] = '1';
    if (t.dst1)
        s[55] = '1';
    putBcd(s, std::clamp(int(std::lround(std::fabs(t.dut1) * 10.0)), 0, 7), kDutSecs, kDutW, 3);
    return s;
}

bool decodeFrame(const std::string &s, Time &t)
{
    if (s.size() != 60)
        return false;
    for (int i = 1; i < 60; ++i)
    {
        const bool marker = i % 10 == 9;
        const char c = s[size_t(i)];
        if (marker ? c != 'M' : (c != '0' && c != '1'))
            return false;
    }
    if (s[0] == 'M' || s[0] == '1')
        return false;
    for (int u : kUnused)
        if (s[size_t(u)] != '0')
            return false;
    if (!bcdDigitsOk(s, kMinSecs, 4) || !bcdDigitsOk(s, kHourSecs, 4) || !bcdDigitsOk(s, kDaySecs, 4)
        || !bcdDigitsOk(s, kDaySecs + 4, 4) || !bcdDigitsOk(s, kYearSecs, 4) || !bcdDigitsOk(s, kYearSecs + 4, 4))
        return false;
    Time r;
    r.minute = getBcd(s, kMinSecs, kMinW, 7);
    r.hour = getBcd(s, kHourSecs, kHourW, 6);
    r.dayOfYear = getBcd(s, kDaySecs, kDayW, 10);
    r.year = 2000 + getBcd(s, kYearSecs, kYearW, 8);
    if (r.minute > 59 || r.hour > 23 || r.dayOfYear < 1 || r.dayOfYear > (isLeap(r.year) ? 366 : 365))
        return false;
    r.dst2 = s[2] == '1';
    r.leapWarning = s[3] == '1';
    r.dst1 = s[55] == '1';
    r.dut1 = getBcd(s, kDutSecs, kDutW, 3) / 10.0 * (s[50] == '1' ? 1.0 : -1.0);
    t = r;
    return true;
}

static Time addMinutes(Time t, long n)
{
    long m = t.minute + n;
    long h = t.hour + (m >= 0 ? m / 60 : -((59 - m) / 60));
    m = ((m % 60) + 60) % 60;
    long d = t.dayOfYear + (h >= 0 ? h / 24 : -((23 - h) / 24));
    h = ((h % 24) + 24) % 24;
    int y = t.year;
    while (d > (isLeap(y) ? 366 : 365))
    {
        d -= isLeap(y) ? 366 : 365;
        ++y;
    }
    while (d < 1)
    {
        --y;
        d += isLeap(y) ? 366 : 365;
    }
    t.minute = int(m);
    t.hour = int(h);
    t.dayOfYear = int(d);
    t.year = y;
    return t;
}

bool gridToLatLon(const std::string &grid, double &lat, double &lon)
{
    if (grid.size() < 4)
        return false;
    const int a = std::toupper(static_cast<unsigned char>(grid[0])) - 'A';
    const int b = std::toupper(static_cast<unsigned char>(grid[1])) - 'A';
    const int c = grid[2] - '0', d = grid[3] - '0';
    if (a < 0 || a > 17 || b < 0 || b > 17 || c < 0 || c > 9 || d < 0 || d > 9)
        return false;
    lon = -180.0 + a * 20.0 + c * 2.0;
    lat = -90.0 + b * 10.0 + d * 1.0;
    if (grid.size() >= 6)
    {
        const int e = std::toupper(static_cast<unsigned char>(grid[4])) - 'A';
        const int f = std::toupper(static_cast<unsigned char>(grid[5])) - 'A';
        if (e < 0 || e > 23 || f < 0 || f > 23)
            return false;
        lon += (e + 0.5) * (2.0 / 24.0);
        lat += (f + 0.5) * (1.0 / 24.0);
    }
    else
    {
        lon += 1.0;
        lat += 0.5;
    }
    return true;
}

double pathDelayMs(double lat, double lon, bool hawaii, double *distanceKm)
{
    // WWV near Fort Collins, Colorado; WWVH Kekaha, Kauai. Middle of each antenna field
    // (NIST SP 250-67, tables 1.2 and 2.7).
    const double slat = hawaii ? 21.9874 : 40.6798, slon = hawaii ? -159.7637 : -105.0408;
    const double r = M_PI / 180.0;
    const double dlat = (slat - lat) * r, dlon = (slon - lon) * r;
    const double h = std::sin(dlat / 2) * std::sin(dlat / 2)
        + std::cos(lat * r) * std::cos(slat * r) * std::sin(dlon / 2) * std::sin(dlon / 2);
    const double d = 2.0 * 6371.0 * std::asin(std::min(1.0, std::sqrt(h)));
    if (distanceKm)
        *distanceKm = d;
    double path = d;
    if (d > 150.0)
    {
        // F-layer hops of at most about 3000 km, reflecting at 300 km.
        const double hops = std::ceil(d / 3000.0);
        const double half = d / hops / 2.0;
        path = hops * 2.0 * std::sqrt(half * half + 300.0 * 300.0);
    }
    return path / 299.792458;
}
} // namespace wwv

// ===========================================================================
// Mixer / frequency meter
// ===========================================================================

void WwvDecoder::Mixer::configure(double fs, double f, double boxSec)
{
    hz = f;
    const double w = 2.0 * M_PI * f / fs;
    rot = {std::cos(w), -std::sin(w)};
    osc = {1.0, 0.0};
    ring.assign(size_t(std::max(1L, std::lround(boxSec * fs))), {0.0, 0.0});
    sum = {0.0, 0.0};
    pos = 0;
    renorm = 0;
}

std::complex<double> WwvDecoder::Mixer::step(double x)
{
    osc *= rot;
    if (++renorm >= 4096)
    {
        renorm = 0;
        osc /= std::abs(osc);
        // The running sum is rebuilt now and then so rounding cannot build up.
        std::complex<double> s {0.0, 0.0};
        for (const auto &v : ring)
            s += v;
        sum = s - ring[pos];
        const std::complex<double> v = x * osc;
        ring[pos] = v;
        sum += v;
        pos = (pos + 1) % ring.size();
        return sum / double(ring.size());
    }
    const std::complex<double> v = x * osc;
    sum += v - ring[pos];
    ring[pos] = v;
    pos = (pos + 1) % ring.size();
    return sum / double(ring.size());
}

void WwvDecoder::FreqMeter::configure(double fs, double hz, double boxSec, double lag)
{
    mix.configure(fs, hz, boxSec);
    // Sample the filtered tone ten times per lag (at least every sample).
    every = size_t(std::max(1L, std::lround(lag / 10.0 * fs)));
    lagSteps = size_t(std::max(1L, std::lround(lag * fs / double(every))));
    lagSec = double(lagSteps * every) / fs;
    count = 0;
    hist.clear();
    acc = {0.0, 0.0};
    norm = lvl = 0.0;
    n = 0;
}

void WwvDecoder::FreqMeter::step(double x)
{
    const std::complex<double> z = mix.step(x);
    if (++count < every)
        return;
    count = 0;
    hist.push_back(z);
    if (hist.size() <= lagSteps)
        return;
    const std::complex<double> z0 = hist.front();
    hist.pop_front();
    acc += z * std::conj(z0);
    norm += std::abs(z) * std::abs(z0);
    lvl += std::abs(z);
    ++n;
}

bool WwvDecoder::FreqMeter::take(double &errHz, double &coherence, double &level)
{
    if (n == 0 || norm <= 0.0)
    {
        acc = {0.0, 0.0};
        norm = lvl = 0.0;
        n = 0;
        return false;
    }
    errHz = std::arg(acc) / (2.0 * M_PI * lagSec);
    coherence = std::abs(acc) / norm;
    level = lvl / double(n);
    acc = {0.0, 0.0};
    norm = lvl = 0.0;
    n = 0;
    return true;
}

// ===========================================================================
// Decoder
// ===========================================================================

WwvDecoder::WwvDecoder() = default;

void WwvDecoder::setSampleRate(double fs)
{
    if (fs == m_fs || fs < 8000.0)
        return;
    m_fs = fs;
    reset();
}

void WwvDecoder::setCarrierMode(bool on)
{
    if (on == m_carrierMode)
        return;
    m_carrierMode = on;
    if (m_fs > 0.0)
        reset();
}

void WwvDecoder::reset()
{
    const bool hawaii = m_st.hawaii;
    m_st = Status();
    m_st.hawaii = hawaii;
    if (m_fs <= 0.0)
        return;
    m_t1000.configure(m_fs, 1000.0, kTickBoxSec);
    m_t1200.configure(m_fs, 1200.0, kTickBoxSec);
    m_t1500.configure(m_fs, 1500.0, kTickBoxSec);
    m_m100.configure(m_fs, 100.0, kCodeBoxSec);
    m_f440.configure(m_fs, 440.0, 0.05, 0.1);
    m_f500.configure(m_fs, 500.0, 0.05, 0.1);
    m_f600.configure(m_fs, 600.0, 0.05, 0.1);
    m_carCoarse.configure(m_fs, 1000.0, 0.004, 0.002);
    m_carCentre = 1000.0;
    m_carFine.configure(m_fs, m_carCentre, 0.05, 0.1);
    m_carSum = m_carWeight = 0.0;
    const size_t ring = size_t(kRingSec * m_fs);
    m_tickRing.assign(ring, 0.0f);
    m_envRing.assign(ring, 0.0f);
    m_fold.assign(1000, 0.0f);
    m_foldCur.assign(1000, 0.0f);
    m_e1000 = m_e1200 = 0.0;
    m_locked = false;
    m_k = 0;
    m_ticks.clear();
    m_missed = 0;
    m_tickLevel = 0.0;
    m_noise = 1e-6;
    m_sym.clear();
    m_minuteTone.clear();
    m_pulseSeen.clear();
    m_haveFrame = false;
    m_toneHold = 0;
    m_toneErr = 0.0;
    m_n = 0;
    m_nextSlow = uint64_t(m_fs);
}

double WwvDecoder::tickEnvAt(double s) const
{
    if (s < 0.0 || s >= double(m_n) || double(m_n) - s >= double(m_tickRing.size()))
        return 0.0;
    return m_tickRing[size_t(s) % m_tickRing.size()];
}

double WwvDecoder::env100At(double s) const
{
    if (s < 0.0 || s >= double(m_n) || double(m_n) - s >= double(m_envRing.size()))
        return 0.0;
    return m_envRing[size_t(s) % m_envRing.size()];
}

void WwvDecoder::process(const float *x, size_t n)
{
    if (m_fs <= 0.0)
        return;
    const uint64_t perSec = uint64_t(std::llround(m_fs));
    for (size_t i = 0; i < n; ++i)
    {
        const double v = x[i];
        if (m_carrierMode)
        {
            m_carCoarse.step(v);
            m_carFine.step(v);
            ++m_n;
            if (m_n >= m_nextSlow)
            {
                m_nextSlow += perSec;
                updateCarrier();
            }
            continue;
        }
        const double e1000 = std::abs(m_t1000.step(v));
        const double e1200 = std::abs(m_t1200.step(v));
        const double e1500 = std::abs(m_t1500.step(v));
        const double e = std::max(e1000, std::max(e1200, e1500));
        const size_t ri = size_t(m_n % m_tickRing.size());
        m_tickRing[ri] = float(e);
        m_envRing[ri] = float(std::abs(m_m100.step(v)));
        m_f440.step(v);
        m_f500.step(v);
        m_f600.step(v);
        // which tick tone is the stronger, slowly
        m_e1000 += (e1000 * e1000 - m_e1000) * (4.0 / m_fs) * (e1000 > 4.0 * m_noise ? 1.0 : 0.0);
        m_e1200 += (e1200 * e1200 - m_e1200) * (4.0 / m_fs) * (e1200 > 4.0 * m_noise ? 1.0 : 0.0);

        const size_t bin = std::min<size_t>(999, size_t(double(m_n % perSec) * 1000.0 / m_fs));
        if (float(e) > m_foldCur[bin])
            m_foldCur[bin] = float(e);
        ++m_n;

        if (m_n % perSec == 0)
        {
            // noise: the median over the second (a tick is 5 ms of it)
            std::vector<float> tmp(m_foldCur);
            std::nth_element(tmp.begin(), tmp.begin() + 500, tmp.end());
            m_noise += 0.3 * (std::max(1e-9, double(tmp[500])) - m_noise);
            for (size_t b = 0; b < 1000; ++b)
                m_fold[b] = 0.8f * m_fold[b] + m_foldCur[b];
            std::fill(m_foldCur.begin(), m_foldCur.end(), 0.0f);
            if (!m_locked)
                searchFold();
        }
        if (m_locked)
        {
            const double tk = m_icept + m_slope * double(m_k);
            if (!m_ticks.empty() && m_ticks.back().first == m_k)
            {
                // already measured
            }
            else if (m_missed >= 0 && double(m_n) >= tk + 0.020 * m_fs && double(m_n) < tk + 0.020 * m_fs + 1.5)
            {
                measureTick();
            }
            if (m_locked && double(m_n) >= tk + m_slope + 0.005 * m_fs)
            {
                onSecondEnd(m_k, tk);
                ++m_k;
            }
        }
        if (m_n >= m_nextSlow)
        {
            m_nextSlow += perSec;
            updateTones();
        }
    }
}

void WwvDecoder::searchFold()
{
    size_t best = 0;
    for (size_t b = 1; b < 1000; ++b)
        if (m_fold[b] > m_fold[best])
            best = b;
    std::vector<float> tmp(m_fold);
    std::nth_element(tmp.begin(), tmp.begin() + 500, tmp.end());
    const double med = std::max(1e-9, double(tmp[500]));
    // After a few seconds of folding, a tick stands well clear of the rest.
    if (m_n < uint64_t(4.0 * m_fs) || m_fold[best] < 4.0 * med)
        return;
    // The 5 ms envelope peaks where the tick ends.
    const double start = double(m_n) - m_fs + (double(best) + 0.5 - 5.0) * m_fs / 1000.0;
    m_slope = m_fs;
    m_icept = start + m_fs;
    while (m_icept + 0.015 * m_fs < double(m_n))
        m_icept += m_fs;
    m_k = 0;
    m_ticks.clear();
    m_missed = 0;
    m_locked = true;
    m_sym.clear();
    m_minuteTone.clear();
    m_pulseSeen.clear();
}

void WwvDecoder::measureTick()
{
    const double expect = m_icept + m_slope * double(m_k);
    const long a = long(std::floor(expect - 0.008 * m_fs)), b = long(std::ceil(expect + 0.014 * m_fs));
    double peak = 0.0;
    long ip = a;
    for (long t = a; t <= b; ++t)
    {
        const double e = tickEnvAt(double(t));
        if (e > peak)
        {
            peak = e;
            ip = t;
        }
    }
    bool ok = peak > 4.0 * m_noise && peak > 0.3 * m_tickLevel;
    double start = 0.0;
    if (ok)
    {
        // Half-height on the rising edge = the middle of the 5 ms average.
        long j = ip;
        while (j > a - long(0.004 * m_fs) && tickEnvAt(double(j)) > 0.5 * peak)
            --j;
        const double e0 = tickEnvAt(double(j)), e1 = tickEnvAt(double(j + 1));
        const double frac = e1 > e0 ? (0.5 * peak - e0) / (e1 - e0) : 0.0;
        start = double(j) + std::clamp(frac, 0.0, 1.0) - 0.5 * kTickBoxSec * m_fs;
        ok = std::fabs(start - expect) < 0.008 * m_fs;
    }
    if (!ok)
    {
        if (++m_missed > 20)
        {
            m_locked = false;
            m_st.ticks = false;
            m_st.code = false;
            std::fill(m_fold.begin(), m_fold.end(), 0.0f);
        }
        // mark this second as looked at
        m_ticks.emplace_back(m_k, -1.0);
        return;
    }
    m_missed = 0;
    m_tickLevel += 0.2 * (peak - m_tickLevel);
    m_ticks.emplace_back(m_k, start);
    while (m_ticks.size() > 600)
        m_ticks.pop_front();
    fitTicks();
}

void WwvDecoder::fitTicks()
{
    std::vector<std::pair<double, double>> pts;
    const long k0 = m_ticks.front().first;
    double t0 = 0.0;
    for (const auto &q : m_ticks)
        if (q.second >= 0.0)
        {
            if (pts.empty())
                t0 = q.second;
            pts.emplace_back(double(q.first - k0), q.second - t0);
        }
    if (pts.size() < 3)
    {
        if (!pts.empty())
            m_icept = (pts.back().second + t0) - m_slope * double(pts.back().first + double(k0));
        return;
    }
    double a = 0.0, b = m_slope;
    for (int pass = 0; pass < 2; ++pass)
    {
        double sk = 0, st = 0, skk = 0, skt = 0;
        const double n = double(pts.size());
        for (const auto &p : pts)
        {
            sk += p.first;
            st += p.second;
            skk += p.first * p.first;
            skt += p.first * p.second;
        }
        const double den = n * skk - sk * sk;
        if (den <= 0.0)
            return;
        b = (n * skt - sk * st) / den;
        a = (st - b * sk) / n;
        if (pass == 0)
        {
            std::vector<std::pair<double, double>> keep;
            for (const auto &p : pts)
                if (std::fabs(p.second - (a + b * p.first)) < 0.001 * m_fs)
                    keep.push_back(p);
            if (keep.size() < 3 || keep.size() == pts.size())
                break;
            pts.swap(keep);
        }
    }
    const double span = pts.back().first - pts.front().first;
    if (pts.size() < 8 || span < 8.0)
    {
        // too few for a rate: follow the phase only
        const auto &p = pts.back();
        m_icept = (p.second + t0) - m_slope * (p.first + double(k0));
        m_st.ticks = pts.size() >= 4;
        return;
    }
    if (std::fabs(b / m_fs - 1.0) > 1000e-6)
        return;
    double rss = 0.0;
    for (const auto &p : pts)
    {
        const double r = p.second - (a + b * p.first);
        rss += r * r;
    }
    m_slope = b;
    m_icept = a + t0 - b * double(k0);
    m_st.ticks = true;
    m_st.clockPpm = (b / m_fs - 1.0) * 1e6;
    m_st.clockPpmSpanSec = span;
    m_st.tickJitterMs = std::sqrt(rss / double(pts.size())) / m_fs * 1000.0;
}

void WwvDecoder::onSecondEnd(long k, double t0)
{
    const double ms = m_slope / 1000.0;
    // The 50 ms average at time t covers [t - 50 ms, t].
    const auto mean = [&](double fromMs, double toMs, bool tick)
    {
        double acc = 0.0;
        int n = 0;
        for (double t = fromMs; t <= toMs; t += 2.0, ++n)
            acc += tick ? tickEnvAt(t0 + t * ms) : env100At(t0 + t * ms);
        return n > 0 ? acc / n : 0.0;
    };
    const double A = mean(85, 195, false), B = mean(255, 495, false), C = mean(555, 795, false),
                 D = mean(860, 995, false);
    const bool seen = A > 2.0 * D && A > 1e-7;
    char sym = '-';
    if (seen)
    {
        const double L = A - D;
        const double b = (B - D) / L, c = (C - D) / L;
        sym = c > 0.5 && b > 0.5 ? 'M' : (b > 0.5 ? '1' : '0');
    }
    const double tone = mean(150, 700, true);
    m_sym.push_back(sym);
    m_pulseSeen.push_back(seen);
    m_minuteTone.push_back(tone > 0.4 * m_tickLevel && tone > 4.0 * m_noise);
    while (m_sym.size() > 120)
    {
        m_sym.pop_front();
        m_pulseSeen.pop_front();
        m_minuteTone.pop_front();
    }
    int seenCount = 0, considered = 0;
    for (size_t i = m_pulseSeen.size() > 20 ? m_pulseSeen.size() - 20 : 0; i < m_pulseSeen.size(); ++i, ++considered)
        seenCount += m_pulseSeen[i];
    m_st.code = considered >= 6 && seenCount * 10 >= considered * 6;
    m_st.symbols.assign(m_sym.end() - std::ptrdiff_t(std::min<size_t>(60, m_sym.size())), m_sym.end());
    m_st.hawaii = m_e1200 > 2.0 * m_e1000;
    tryFrame();

    // The second that has just begun.
    const long cur = k + 1;
    m_st.timeValid = m_haveFrame;
    if (m_haveFrame)
    {
        const long el = cur - m_frameK;
        m_st.time = wwv::addMinutes(m_frameTime, el >= 0 ? el / 60 : -((59 - el) / 60));
        m_st.secondInMinute = int(((el % 60) + 60) % 60);
        m_st.tickSample = m_icept + m_slope * double(cur);
    }
}

void WwvDecoder::tryFrame()
{
    if (m_sym.size() < 60)
        return;
    const size_t off = m_sym.size() - 60;
    if (m_sym.back() != 'M' || m_sym[off + 9] != 'M')
        return;
    // Second 0 has no pulse and carries the long minute tone; markers alone
    // repeat every ten seconds and would fit six ways.
    if (m_sym[off] != '-' && !m_minuteTone[off])
        return;
    const std::string s(m_sym.begin() + std::ptrdiff_t(off), m_sym.end());
    wwv::Time t;
    if (!wwv::decodeFrame(s, t))
        return;
    const long frameK = m_k - 59;
    bool verified = false;
    if (m_haveFrame)
    {
        const long dk = frameK - m_frameK;
        verified
            = dk > 0 && dk % 60 == 0 && dk <= 600 && t.minutesSince2000() - m_frameTime.minutesSince2000() == dk / 60;
    }
    m_haveFrame = true;
    m_frameTime = t;
    m_frameK = frameK;
    ++m_st.framesDecoded;
    // Once verified it stays so while frames keep agreeing; a frame that
    // disagrees starts again.
    m_st.timeVerified = verified;
}

void WwvDecoder::updateTones()
{
    struct R
    {
        int hz;
        double err, coh, lvl;
        bool ok;
    } r[3] = {{440, 0, 0, 0, false}, {500, 0, 0, 0, false}, {600, 0, 0, 0, false}};
    r[0].ok = m_f440.take(r[0].err, r[0].coh, r[0].lvl);
    r[1].ok = m_f500.take(r[1].err, r[1].coh, r[1].lvl);
    r[2].ok = m_f600.take(r[2].err, r[2].coh, r[2].lvl);
    const R *best = nullptr;
    for (const R &q : r)
        if (q.ok && q.coh > 0.85 && q.lvl > 3.0 * m_noise && (!best || q.lvl > best->lvl))
            best = &q;
    if (best)
    {
        if (m_st.toneHz != best->hz || m_toneHold == 0)
            m_toneErr = best->err;
        else
            m_toneErr += 0.2 * (best->err - m_toneErr);
        m_st.toneHz = best->hz;
        m_st.toneErrorHz = m_toneErr;
        m_toneHold = 3;
    }
    else if (m_toneHold > 0)
        --m_toneHold;
    m_st.tones = m_toneHold > 0;
}

void WwvDecoder::updateCarrier()
{
    double err = 0, coh = 0, lvl = 0;
    if (m_carCoarse.take(err, coh, lvl) && coh > 0.6)
    {
        const double f0 = 1000.0 + err;
        if (std::fabs(f0 - m_carCentre) > 3.0 && f0 > 700.0 && f0 < 1300.0)
        {
            m_carCentre = std::round(f0 * 10.0) / 10.0;
            m_carFine.configure(m_fs, m_carCentre, 0.05, 0.1);
            m_carSum = m_carWeight = 0.0;
            m_st.carrier = false;
            return;
        }
    }
    else
    {
        // no steady carrier this second
        double e2, c2, l2;
        m_carFine.take(e2, c2, l2);
        return;
    }
    if (m_carFine.take(err, coh, lvl) && coh > 0.9)
    {
        m_carSum += m_carCentre + err;
        m_carWeight += 1.0;
        m_st.carrierHz = m_carSum / m_carWeight;
        m_st.carrierSeconds = m_carWeight;
        m_st.carrier = m_carWeight >= 3.0;
    }
}

// ===========================================================================
// Encoder
// ===========================================================================

void WwvEncoder::start(const wwv::Time &start, int second, bool hawaii)
{
    m_time = start;
    m_frame = wwv::encodeFrame(m_time);
    m_sec = std::clamp(second, 0, 59);
    m_t = 0.0;
    m_hawaii = hawaii;
    m_ph100 = m_phTick = m_phTone = 0.0;
}

void WwvEncoder::render(float *out, size_t n, double fs)
{
    const double dt = 1.0 / fs;
    const double tickHz = m_hawaii ? 1200.0 : 1000.0;
    for (size_t i = 0; i < n; ++i)
    {
        double v = 0.0;
        // tick / minute tone (none at seconds 29 and 59)
        const double tickLen = m_sec == 0 ? 0.8 : (m_sec == 29 || m_sec == 59 ? 0.0 : 0.005);
        const double hz = m_sec == 0 && m_time.minute == 0 ? 1500.0 : tickHz;
        if (m_t < tickLen)
        {
            v += std::sin(m_phTick);
            m_phTick += 2.0 * M_PI * hz * dt;
        }
        // 100 Hz code
        const char s = m_frame[size_t(m_sec)];
        const double end = s == 'M' ? 0.8 : (s == '1' ? 0.5 : (s == '0' ? 0.2 : 0.0));
        m_ph100 += 2.0 * M_PI * 100.0 * dt;
        if (m_ph100 > 2.0 * M_PI)
            m_ph100 -= 2.0 * M_PI;
        if (m_t >= 0.030 && m_t < end)
            v += m_codeLevel * std::sin(m_ph100);
        // 500 / 600 Hz tone for the first 45 s, cut around each tick
        const bool even = m_time.minute % 2 == 0;
        const double toneHz = (even != m_hawaii) ? 500.0 : 600.0;
        m_phTone += 2.0 * M_PI * toneHz * dt;
        if (m_phTone > 2.0 * M_PI)
            m_phTone -= 2.0 * M_PI;
        if (m_sec >= 1 && m_sec < 45 && m_t >= 0.040 && m_t < 0.990)
            v += 0.5 * std::sin(m_phTone);
        out[i] = float(0.5 * v);
        m_t += dt;
        if (m_t >= 1.0)
        {
            m_t -= 1.0;
            m_phTick = 0.0;
            if (++m_sec >= 60)
            {
                m_sec = 0;
                m_time = wwv::addMinutes(m_time, 1);
                m_frame = wwv::encodeFrame(m_time);
            }
        }
    }
}
