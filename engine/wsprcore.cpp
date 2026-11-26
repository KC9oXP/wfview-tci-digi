#include "wsprcore.h"

#include "wsprd/nhash.h"
#include "wsprd/wsprd.h"
#include "wsprd/wsprsim_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace wspr
{

// ---- messages --------------------------------------------------------------

bool validDbm(int dbm)
{
    if (dbm < 0 || dbm > 60)
        return false;
    const int u = dbm % 10;
    return u == 0 || u == 3 || u == 7;
}

int nearestDbm(int dbm)
{
    dbm = std::clamp(dbm, 0, 60);
    int best = 0;
    for (int d = 0; d <= 60; ++d)
        if (validDbm(d) && std::abs(d - dbm) < std::abs(best - dbm))
            best = d;
    return best;
}

namespace
{
std::string upper(std::string s)
{
    for (char &c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool isAlnum(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}
bool isLetter(char c)
{
    return c >= 'A' && c <= 'Z';
}

std::string urlEncode(const std::string &s)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += char(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
} // namespace

bool standardCall(const std::string &in)
{
    const std::string c = upper(in);
    if (c.size() < 3 || c.size() > 6)
        return false;
    for (char ch : c)
        if (!isAlnum(ch))
            return false;
    // Same alignment as the encoder: the digit goes in the 3rd place.
    std::string p;
    if (isDigit(c[2]))
        p = c;
    else if (isDigit(c[1]))
        p = " " + c;
    else
        return false;
    if (p.size() > 6)
        return false;
    while (p.size() < 6)
        p += ' ';
    if (!(p[0] == ' ' || isAlnum(p[0])) || !isAlnum(p[1]) || !isDigit(p[2]))
        return false;
    for (int k = 3; k < 6; ++k)
        if (!(p[size_t(k)] == ' ' || isLetter(p[size_t(k)])))
            return false;
    // No letters after a space ("K1 A" cannot happen here, but keep the suffix contiguous).
    bool space = false;
    for (int k = 3; k < 6; ++k)
    {
        if (p[size_t(k)] == ' ')
            space = true;
        else if (space)
            return false;
    }
    return true;
}

bool compoundCall(const std::string &in)
{
    const std::string c = upper(in);
    const size_t slash = c.find('/');
    if (slash == std::string::npos || c.find('/', slash + 1) != std::string::npos)
        return false;
    const std::string a = c.substr(0, slash), b = c.substr(slash + 1);
    auto allAlnum = [](const std::string &s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char ch) { return isAlnum(ch); });
    };
    if (!allAlnum(a) || !allAlnum(b))
        return false;
    if (a.size() <= 3 && standardCall(b))
        return true; // prefix: PJ4/K1ABC
    if (standardCall(a) && (b.size() == 1 || (b.size() == 2 && isDigit(b[0]) && isDigit(b[1]))))
        return true; // suffix: K1ABC/P, K1ABC/12
    return false;
}

bool validGrid(const std::string &in)
{
    const std::string g = upper(in);
    if (g.size() != 4 && g.size() != 6)
        return false;
    if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R' || !isDigit(g[2]) || !isDigit(g[3]))
        return false;
    if (g.size() == 6 && (g[4] < 'A' || g[4] > 'X' || g[5] < 'A' || g[5] > 'X'))
        return false;
    return true;
}

std::vector<std::string> beaconMessages(const std::string &callIn, const std::string &gridIn, int dbm,
                                        std::string *err)
{
    auto fail = [&](const char *why) {
        if (err)
            *err = why;
        return std::vector<std::string>();
    };
    const std::string call = upper(callIn), grid = upper(gridIn);
    if (!validDbm(dbm))
        return fail("Power must be 0, 3, 7, 10, 13 ... 60 dBm");
    if (!validGrid(grid))
        return fail("Grid must be 4 or 6 characters (EN71 or EN71AR)");
    char p[4];
    std::snprintf(p, sizeof(p), "%d", dbm);
    const std::string pw(p);
    std::vector<std::string> out;
    if (standardCall(call))
    {
        out.push_back(call + " " + grid.substr(0, 4) + " " + pw);
        if (grid.size() == 6)
            out.push_back("<" + call + "> " + grid + " " + pw);
    }
    else if (compoundCall(call))
    {
        out.push_back(call + " " + pw);
        if (grid.size() == 6)
            out.push_back("<" + call + "> " + grid + " " + pw);
    }
    else
        return fail("This call cannot be sent in WSPR (up to 6 characters with a digit in the 2nd or 3rd "
                    "place, or one / prefix or suffix)");
    if (err)
        err->clear();
    return out;
}

bool encode(const std::string &message, std::array<std::uint8_t, kSymbols> &symbols)
{
    // The encoder checks its result with the decoder's unpacker, which wants hash tables.
    std::vector<char> hash(size_t(HASHTAB_SIZE) * HASHTAB_ENTRY_LEN, 0), loc(size_t(HASHTAB_SIZE) * LOCTAB_ENTRY_LEN, 0);
    char msg[23] = {0};
    std::snprintf(msg, sizeof(msg), "%s", upper(message).c_str());
    unsigned char sym[kSymbols] = {0};
    if (!wsprd::get_wspr_channel_symbols(msg, hash.data(), loc.data(), sym))
        return false;
    for (int k = 0; k < kSymbols; ++k)
    {
        if (sym[k] > 3)
            return false;
        symbols[size_t(k)] = sym[k];
    }
    return true;
}

// ---- transmit --------------------------------------------------------------

void Modulator::start(const std::array<std::uint8_t, kSymbols> &symbols, double centreHz)
{
    m_sym = symbols;
    m_centre = std::clamp(centreHz, kCentreHz - kWindowHalfHz + 3.0, kCentreHz + kWindowHalfHz - 3.0);
    m_t = 0.0;
    m_phase = 0.0;
    m_active = true;
}

double Modulator::progress() const
{
    return std::clamp(m_t / kTxSeconds, 0.0, 1.0);
}

int Modulator::symbolIndex() const
{
    return std::clamp(int(m_t / kSymbolSec), 0, kSymbols - 1);
}

void Modulator::render(float *out, size_t n, double fs)
{
    if (!m_active || fs <= 0.0)
    {
        std::fill(out, out + n, 0.0f);
        return;
    }
    const double dt = 1.0 / fs;
    constexpr double ramp = 0.005;
    for (size_t k = 0; k < n; ++k)
    {
        if (m_t >= kTxSeconds)
        {
            m_active = false;
            std::fill(out + k, out + n, 0.0f);
            return;
        }
        const int s = std::min(int(m_t / kSymbolSec), kSymbols - 1);
        const double f = m_centre + (double(m_sym[size_t(s)]) - 1.5) * kToneSpacing;
        double a = 1.0;
        if (m_t < ramp)
            a = m_t / ramp;
        else if (kTxSeconds - m_t < ramp)
            a = (kTxSeconds - m_t) / ramp;
        out[k] = float(a * std::sin(m_phase));
        m_phase += 2.0 * M_PI * f * dt;
        if (m_phase > 2.0 * M_PI)
            m_phase -= 2.0 * M_PI;
        m_t += dt;
    }
}

// ---- receive ---------------------------------------------------------------

namespace
{
std::vector<double> lowpass(int taps, double cutoffHz, double fs)
{
    std::vector<double> h(static_cast<size_t>(taps), 0.0);
    const double fc = cutoffHz / fs;
    const int m = taps - 1;
    double sum = 0.0;
    for (int n = 0; n < taps; ++n)
    {
        const double x = n - m / 2.0;
        const double sinc = std::fabs(x) < 1e-12 ? 2.0 * fc : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
        const double w = 0.42 - 0.5 * std::cos(2.0 * M_PI * n / m) + 0.08 * std::cos(4.0 * M_PI * n / m);
        h[size_t(n)] = sinc * w;
        sum += h[size_t(n)];
    }
    for (double &v : h)
        v /= sum;
    return h;
}

void decimate(const std::vector<std::complex<double>> &in, const std::vector<double> &h, int factor,
              std::vector<std::complex<double>> &out)
{
    out.clear();
    const int taps = int(h.size());
    const int half = taps / 2;
    out.reserve(in.size() / size_t(factor) + 1);
    for (size_t c = 0; c < in.size(); c += size_t(factor))
    {
        std::complex<double> acc = 0.0;
        for (int k = 0; k < taps; ++k)
        {
            const long idx = long(c) + half - k;
            if (idx >= 0 && size_t(idx) < in.size())
                acc += in[size_t(idx)] * h[size_t(k)];
        }
        out.push_back(acc);
    }
}
} // namespace

namespace
{
// Mix 1500 Hz to 0, then 12000 -> 1500 (/8) -> 375 (/4).
void basebandOf(const float *audio, size_t len, std::vector<std::complex<double>> &b)
{
    std::vector<std::complex<double>> x(len);
    const double w = 2.0 * M_PI * kCentreHz / kAudioRate;
    for (size_t n = 0; n < len; ++n)
        x[n] = double(audio[n]) * std::polar(1.0, -w * double(n));
    static const std::vector<double> h1 = lowpass(127, 600.0, 12000.0);
    static const std::vector<double> h2 = lowpass(97, 170.0, 1500.0);
    std::vector<std::complex<double>> a;
    decimate(x, h1, 8, a);
    decimate(a, h2, 4, b);
}
} // namespace

void toBaseband(const std::vector<float> &audio, std::vector<float> &i, std::vector<float> &q)
{
    std::vector<std::complex<double>> b;
    basebandOf(audio.data(), audio.size(), b);
    const size_t want = size_t(120.0 * kBasebandRate); // the decoder's buffer (zeros after the recording)
    i.assign(want, 0.0f);
    q.assign(want, 0.0f);
    for (size_t n = 0; n < std::min(want, b.size()); ++n)
    {
        i[n] = float(b[n].real() * 1000.0);
        q[n] = float(b[n].imag() * 1000.0);
    }
}

Decoder::Decoder()
    : m_hash(size_t(HASHTAB_SIZE) * HASHTAB_ENTRY_LEN, 0)
    , m_loc(size_t(HASHTAB_SIZE) * LOCTAB_ENTRY_LEN, 0)
{
}

namespace
{
std::vector<std::string> words(const std::string &s)
{
    std::istringstream in(s);
    std::vector<std::string> w;
    std::string t;
    while (in >> t)
        w.push_back(t);
    return w;
}
} // namespace

std::vector<Spot> Decoder::decode(const std::vector<float> &iIn, const std::vector<float> &qIn, std::int64_t dialHz,
                                  int passes)
{
    std::vector<float> i = iIn, q = qIn; // the decoder subtracts signals in place
    const size_t want = size_t(120.0 * kBasebandRate);
    i.resize(want, 0.0f);
    q.resize(want, 0.0f);
    wsprd::decoder_options opt {};
    opt.freq = int(std::clamp<std::int64_t>(dialHz, 0, 2000000000));
    opt.quickmode = passes <= 1 ? 1 : 0;
    opt.usehashtable = 1;
    opt.npasses = std::clamp(passes, 1, 3);
    opt.subtraction = passes <= 1 ? 0 : 1;
    opt.hashtab = m_hash.data();
    opt.loctab = m_loc.data();
    std::vector<wsprd::decoder_results> res(MAX_UNIQUES);
    int n = 0;
    wsprd::wspr_decode(i.data(), q.data(), int(want), opt, res.data(), &n);
    std::vector<Spot> out;
    for (int k = 0; k < n; ++k)
    {
        const wsprd::decoder_results &r = res[size_t(k)];
        Spot s;
        s.message = r.message;
        s.snr = r.snr;
        s.dt = r.dt;
        s.drift = r.drift;
        s.cycles = r.cycles;
        s.rfHz = std::llround(r.freq * 1e6);
        s.audioHz = double(s.rfHz - dialHz);
        // Parse the message itself (the decoder's call/loc/pwr are not set for Type 2).
        const std::vector<std::string> w = words(s.message);
        if (w.size() == 3)
        {
            s.call = w[0];
            s.grid = w[1];
            s.dbm = std::atoi(w[2].c_str());
        }
        else if (w.size() == 2)
        {
            s.call = w[0];
            s.dbm = std::atoi(w[1].c_str());
        }
        else
            continue;
        if (s.call.size() > 2 && s.call.front() == '<' && s.call.back() == '>' && s.call != "<...>")
            s.call = s.call.substr(1, s.call.size() - 2);
        out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const Spot &a, const Spot &b) { return a.audioHz < b.audioHz; });
    return out;
}

std::string Decoder::saveHashes() const
{
    std::string out;
    char line[64];
    for (int k = 0; k < HASHTAB_SIZE; ++k)
    {
        const char *c = &m_hash[size_t(k) * HASHTAB_ENTRY_LEN];
        if (!c[0])
            continue;
        const char *g = &m_loc[size_t(k) * LOCTAB_ENTRY_LEN];
        std::snprintf(line, sizeof(line), "%d %.12s %.4s\n", k, c, g);
        out += line;
    }
    return out;
}

void Decoder::loadHashes(const std::string &text)
{
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l))
    {
        const std::vector<std::string> w = words(l);
        if (w.size() < 2)
            continue;
        const int k = std::atoi(w[0].c_str());
        if (k < 0 || k >= HASHTAB_SIZE || w[1].size() >= HASHTAB_ENTRY_LEN)
            continue;
        // Only keep entries that really belong to this hash.
        if (int(wsprd::nhash(w[1].c_str(), w[1].size(), 146u)) != k)
            continue;
        std::snprintf(&m_hash[size_t(k) * HASHTAB_ENTRY_LEN], HASHTAB_ENTRY_LEN, "%s", w[1].c_str());
        if (w.size() >= 3 && w[2].size() < LOCTAB_ENTRY_LEN)
            std::snprintf(&m_loc[size_t(k) * LOCTAB_ENTRY_LEN], LOCTAB_ENTRY_LEN, "%s", w[2].c_str());
    }
}

size_t Decoder::knownCalls() const
{
    size_t n = 0;
    for (int k = 0; k < HASHTAB_SIZE; ++k)
        if (m_hash[size_t(k) * HASHTAB_ENTRY_LEN])
            ++n;
    return n;
}

WindowSpectrum windowSpectrum(const std::vector<float> &i, const std::vector<float> &q)
{
    WindowSpectrum out;
    constexpr int N = FFT_SIZE;
    out.binHz = kBasebandRate / N;
    const int span = int(std::ceil((kWindowHalfHz + 10.0) / out.binHz));
    out.firstHz = kCentreHz - span * out.binHz;
    std::vector<double> acc(size_t(2 * span + 1), 0.0);
    std::complex<float> buf[N];
    int blocks = 0;
    const size_t len = std::min(i.size(), q.size());
    for (size_t start = 0; start + N <= len; start += N / 2)
    {
        bool any = false;
        for (int k = 0; k < N; ++k)
        {
            const float w = float(0.5 - 0.5 * std::cos(2.0 * M_PI * k / N));
            buf[k] = std::complex<float>(i[start + size_t(k)] * w, q[start + size_t(k)] * w);
            any = any || i[start + size_t(k)] != 0.0f;
        }
        if (!any)
            continue; // zero padding after the recording
        wsprd::fft512(buf);
        for (int b = -span; b <= span; ++b)
        {
            const int k = (b + N) % N;
            acc[size_t(b + span)] += double(std::norm(buf[k]));
        }
        ++blocks;
    }
    out.db.resize(acc.size());
    for (size_t k = 0; k < acc.size(); ++k)
        out.db[k] = float(10.0 * std::log10(blocks ? acc[k] / blocks + 1e-20 : 1e-20));
    return out;
}

WindowSpectrum liveLine(const float *audio, size_t n)
{
    WindowSpectrum out;
    constexpr int N = FFT_SIZE;
    if (n < 16000)
        return out;
    std::vector<std::complex<double>> b;
    basebandOf(audio + (n - 16000), 16000, b); // 500 baseband samples... plus filter edges
    // 16000 / 32 = 500 < 512: zero-pad the few missing; skip the filter's start-up (first 8).
    std::complex<float> buf[N];
    const size_t skip = 8;
    const size_t have = b.size() > skip ? b.size() - skip : 0;
    for (int k = 0; k < N; ++k)
    {
        const float w = float(0.5 - 0.5 * std::cos(2.0 * M_PI * k / N));
        const std::complex<double> v = size_t(k) < have ? b[skip + size_t(k)] : std::complex<double>(0.0, 0.0);
        buf[k] = std::complex<float>(float(v.real()) * w, float(v.imag()) * w);
    }
    wsprd::fft512(buf);
    out.binHz = kBasebandRate / N;
    const int span = int(std::ceil((kWindowHalfHz + 10.0) / out.binHz));
    out.firstHz = kCentreHz - span * out.binHz;
    out.db.resize(size_t(2 * span + 1));
    for (int bIdx = -span; bIdx <= span; ++bIdx)
        out.db[size_t(bIdx + span)] = float(10.0 * std::log10(double(std::norm(buf[(bIdx + N) % N])) + 1e-20));
    return out;
}

// ---- free offsets ----------------------------------------------------------

void FreeFinder::clear()
{
    m_slots.clear();
}

void FreeFinder::addSlot(std::int64_t slotIndex, const std::vector<Spot> &spots, const WindowSpectrum &sp)
{
    Slot s;
    s.index = slotIndex;
    for (const Spot &p : spots)
    {
        const double half = kSignalWidthHz / 2.0 + std::fabs(p.drift) + kGuardHz;
        s.ranges.push_back({p.audioHz - half, p.audioHz + half, true, p.call});
    }
    if (!sp.db.empty())
    {
        std::vector<float> sorted;
        for (size_t k = 0; k < sp.db.size(); ++k)
        {
            const double hz = sp.firstHz + double(k) * sp.binHz;
            if (std::fabs(hz - kCentreHz) <= kWindowHalfHz + 5.0)
                sorted.push_back(sp.db[k]);
        }
        if (!sorted.empty())
        {
            std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(sorted.size() / 2), sorted.end());
            const float floorDb = sorted[sorted.size() / 2];
            // Runs of at least 2 bins above the floor are energy from a station.
            size_t k = 0;
            while (k < sp.db.size())
            {
                if (sp.db[k] < floorDb + float(kEnergyDb))
                {
                    ++k;
                    continue;
                }
                size_t e = k;
                while (e + 1 < sp.db.size() && sp.db[e + 1] >= floorDb + float(kEnergyDb))
                    ++e;
                if (e > k)
                {
                    const double lo = sp.firstHz + double(k) * sp.binHz - kGuardHz;
                    const double hi = sp.firstHz + double(e) * sp.binHz + kGuardHz;
                    s.ranges.push_back({lo, hi, false, std::string()});
                }
                k = e + 1;
            }
        }
    }
    m_slots.erase(std::remove_if(m_slots.begin(), m_slots.end(),
                                 [&](const Slot &o) { return o.index == slotIndex || o.index <= slotIndex - kKeepSlots; }),
                  m_slots.end());
    m_slots.push_back(s);
}

int FreeFinder::slotsSeen(std::int64_t now) const
{
    int n = 0;
    for (const Slot &s : m_slots)
        if (s.index > now - kKeepSlots && s.index <= now)
            ++n;
    return n;
}

std::vector<FreeFinder::Busy> FreeFinder::busy(std::int64_t now) const
{
    std::vector<Busy> all;
    for (const Slot &s : m_slots)
        if (s.index > now - kKeepSlots && s.index <= now)
            all.insert(all.end(), s.ranges.begin(), s.ranges.end());
    std::sort(all.begin(), all.end(), [](const Busy &a, const Busy &b) { return a.lowHz < b.lowHz; });
    std::vector<Busy> merged;
    for (const Busy &b : all)
    {
        if (!merged.empty() && b.lowHz <= merged.back().highHz)
        {
            Busy &m = merged.back();
            m.highHz = std::max(m.highHz, b.highHz);
            if (b.decoded)
            {
                if (!m.decoded)
                    m.call = b.call;
                else if (!b.call.empty() && m.call.find(b.call) == std::string::npos)
                    m.call += ", " + b.call;
                m.decoded = true;
            }
        }
        else
            merged.push_back(b);
    }
    return merged;
}

double FreeFinder::clearance(std::int64_t now, double centreHz) const
{
    const double half = kSignalWidthHz / 2.0;
    double best = 1e9;
    for (const Busy &b : busy(now))
    {
        double d;
        if (centreHz + half < b.lowHz)
            d = b.lowHz - (centreHz + half);
        else if (centreHz - half > b.highHz)
            d = (centreHz - half) - b.highHz;
        else
            d = 0.0;
        best = std::min(best, d);
    }
    return best;
}

std::vector<FreeFinder::Pick> FreeFinder::suggest(std::int64_t now, int count, double minApart) const
{
    struct C
    {
        int hz;
        double clear, score;
    };
    std::vector<C> c;
    for (int hz = int(kCentreHz - kWindowHalfHz + 10); hz <= int(kCentreHz + kWindowHalfHz - 10); ++hz)
    {
        const double cl = clearance(now, hz);
        // Prefer wide gaps (up to 25 Hz counts), then nearer the middle.
        c.push_back({hz, cl, std::min(cl, 25.0) - 0.02 * std::fabs(hz - kCentreHz)});
    }
    std::stable_sort(c.begin(), c.end(), [](const C &a, const C &b) { return a.score > b.score; });
    std::vector<Pick> out;
    for (const C &x : c)
    {
        if (int(out.size()) >= count)
            break;
        if (x.clear <= 0.0)
            break;
        bool near = false;
        for (const Pick &p : out)
            near = near || std::abs(p.hz - x.hz) < minApart;
        if (!near)
            out.push_back({x.hz, x.clear});
    }
    return out;
}

// ---- band hopping ----------------------------------------------------------

const std::vector<Band> &bands()
{
    static const std::vector<Band> b = {
        {"160 m", 1836600},   {"80 m", 3568600},    {"60 m", 5287200},    {"60 m (5.3647)", 5364700},
        {"40 m", 7038600},    {"30 m", 10138700},   {"20 m", 14095600},   {"17 m", 18104600},
        {"15 m", 21094600},   {"12 m", 24924600},   {"10 m", 28124600},   {"6 m", 50293000},
        {"2 m", 144489000},
    };
    return b;
}

int bandIndex(const std::string &name)
{
    const std::vector<Band> &b = bands();
    for (size_t k = 0; k < b.size(); ++k)
        if (name == b[k].name)
            return int(k);
    return -1;
}

int bandForDial(std::int64_t dialHz)
{
    const std::vector<Band> &b = bands();
    for (size_t k = 0; k < b.size(); ++k)
        if (std::llabs(dialHz - b[k].dialHz) <= 2000)
            return int(k);
    return -1;
}

int coordinatedBand(int minuteOfHour)
{
    static const char *order[10] = {"160 m", "80 m", "60 m", "40 m", "30 m", "20 m", "17 m", "15 m", "12 m", "10 m"};
    const int slot = ((minuteOfHour % 60 + 60) % 60) / 2;
    return bandIndex(order[slot % 10]);
}

int HopPlan::bandFor(int minuteOfDay) const
{
    auto ticked = [&](int b) { return b >= 0 && size_t(b) < rx.size() && rx[size_t(b)]; };
    if (mode == Coordinated)
    {
        const int b = coordinatedBand(minuteOfDay % 60);
        return ticked(b) ? b : -1;
    }
    if (mode == Rotate)
    {
        std::vector<int> list;
        for (size_t k = 0; k < rx.size(); ++k)
            if (rx[k])
                list.push_back(int(k));
        if (list.empty())
            return -1;
        const int slot = ((minuteOfDay % 1440 + 1440) % 1440) / 2;
        return list[size_t(slot) % list.size()];
    }
    return -1;
}

// ---- measured power --------------------------------------------------------

void PowerMeter::add(double watts, double secondsIntoTx)
{
    if (secondsIntoTx < 3.0 || !std::isfinite(watts) || watts < 0.0)
        return;
    m_w.push_back(watts);
}

bool PowerMeter::result(double &watts, std::string *why) const
{
    auto fail = [&](const char *t) {
        if (why)
            *why = t;
        return false;
    };
    if (m_w.size() < 5)
        return fail("too few power readings from the radio");
    std::vector<double> v = m_w;
    std::sort(v.begin(), v.end());
    const double med = v[v.size() / 2];
    if (med < 0.001)
        return fail("the radio reported no output");
    const double q1 = v[v.size() / 4], q3 = v[(3 * v.size()) / 4];
    if ((q3 - q1) > 0.3 * med)
        return fail("the power readings were not steady");
    watts = med;
    if (why)
        why->clear();
    return true;
}

int dbmForWatts(double watts, double lossDb)
{
    if (!(watts > 0.0))
        return 0;
    const double dbm = 10.0 * std::log10(watts * 1000.0) - std::max(0.0, lossDb);
    return nearestDbm(int(std::lround(dbm)));
}

// ---- transmit schedule -----------------------------------------------------

bool TxScheduler::decide(std::int64_t slot, double r)
{
    if (m_percent <= 0)
        return false;
    bool tx;
    if (m_percent >= 100)
        tx = true;
    else if (m_percent > 50)
        tx = r < m_percent / 100.0;
    else
    {
        const double p = m_percent / 100.0;
        const std::int64_t maxGap = std::int64_t(std::ceil(2.0 * 100.0 / m_percent));
        if (slot == m_lastTx + 1)
            tx = false;
        else if (slot - m_lastTx >= maxGap)
            tx = true;
        else
            tx = r < p / (1.0 - p); // with the slot after a transmission skipped, this averages p
    }
    if (tx)
        m_lastTx = slot;
    return tx;
}

// ---- WSPRnet ---------------------------------------------------------------

std::string wsprnetSpotQuery(const std::string &rcall, const std::string &rgrid, std::int64_t rxDialHz, const Spot &s,
                             int yymmdd, int hhmm, const std::string &version)
{
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "function=wspr&rcall=%s&rgrid=%s&rqrg=%.6f&date=%06d&time=%04d&sig=%.0f&dt=%.1f&drift=%d"
                  "&tqrg=%.6f&tcall=%s&tgrid=%s&dbm=%d&version=%s&mode=2",
                  urlEncode(upper(rcall)).c_str(), urlEncode(rgrid).c_str(), double(rxDialHz) / 1e6, yymmdd, hhmm,
                  s.snr, s.dt, int(std::lround(s.drift)), double(s.rfHz) / 1e6, urlEncode(s.call).c_str(),
                  urlEncode(s.grid).c_str(), s.dbm, urlEncode(version).c_str());
    return buf;
}

std::string wsprnetStatusQuery(const std::string &rcall, const std::string &rgrid, std::int64_t rxDialHz, int txPercent,
                               std::int64_t txDialHz, int dbm, const std::string &version)
{
    char buf[384];
    std::snprintf(buf, sizeof(buf),
                  "function=wsprstat&rcall=%s&rgrid=%s&rqrg=%.6f&tpct=%d&tqrg=%.6f&dbm=%d&version=%s&mode=2",
                  urlEncode(upper(rcall)).c_str(), urlEncode(rgrid).c_str(), double(rxDialHz) / 1e6, txPercent,
                  double(txDialHz) / 1e6, dbm, urlEncode(version).c_str());
    return buf;
}

} // namespace wspr
