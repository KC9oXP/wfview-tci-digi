#include "hellcore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
struct Glyph
{
    char c;
    const char *rows[7];
};

// 5x7 dot-matrix font.
const Glyph kFont[] = {
    {' ', {".....", ".....", ".....", ".....", ".....", ".....", "....."}},
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {',', {".....", ".....", ".....", ".....", ".##..", "..#..", ".#..."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
    {'/', {".....", "....#", "...#.", "..#..", ".#...", "#....", "....."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'!', {"..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."}},
    {'\'', {"..#..", "..#..", ".#...", ".....", ".....", ".....", "....."}},
    {'"', {".#.#.", ".#.#.", ".....", ".....", ".....", ".....", "....."}},
    {':', {".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."}},
    {';', {".....", ".##..", ".##..", ".....", ".##..", "..#..", ".#..."}},
    {'(', {"...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."}},
    {')', {".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."}},
    {'@', {".###.", "#...#", "#.###", "#.#.#", "#.###", "#....", ".####"}},
    {'*', {".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", "....."}},
    {'&', {".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#"}},
    {'#', {".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#."}},
    {'<', {"...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."}},
    {'>', {".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."}},
    {'_', {".....", ".....", ".....", ".....", ".....", ".....", "#####"}},
    {'$', {"..#..", ".####", "#.#..", ".###.", "..#.#", "####.", "..#.."}},
    {'%', {"##...", "##..#", "...#.", "..#..", ".#...", "#..##", "...##"}},
};

const std::map<char, const Glyph *> &fontMap()
{
    // Built once, thread-safely (static local initialisation).
    static const std::map<char, const Glyph *> m = []()
    {
        std::map<char, const Glyph *> t;
        for (const Glyph &g : kFont)
            t[g.c] = &g;
        return t;
    }();
    return m;
}
} // namespace

namespace hell
{
const char *const *glyph(char c)
{
    const auto &m = fontMap();
    const auto it = m.find(char(std::toupper(static_cast<unsigned char>(c))));
    return it == m.end() ? nullptr : it->second->rows;
}

bool charColumns(char c, std::vector<uint8_t> &cols)
{
    cols.assign(size_t(kCharCols * kRows), 0);
    const char *const *g = glyph(c);
    if (!g)
        return false;
    for (int x = 0; x < kGlyphCols; ++x)
        for (int r = 0; r < 7; ++r)
            if (g[r][x] == '#')
            {
                const int y = 2 * (6 - r); // bottom row -> half-pixels 0, 1
                cols[size_t(x * kRows + y)] = 1;
                cols[size_t(x * kRows + y + 1)] = 1;
            }
    return true;
}
} // namespace hell

// ---------------------------------------------------------------------------

void HellModulator::clear()
{
    m_q.clear();
    m_queued = m_sent = 0;
    m_inCol = false;
    m_filler = false;
    m_px = 0.0;
    m_prevTarget = m_curTarget = 0.0;
    m_curPx = -1;
}

void HellModulator::preroll(double sec)
{
    clear();
    const int n = std::max(1, int(std::lround(sec * hell::kPixelRate / hell::kRows)));
    for (int i = 0; i < n; ++i)
        m_q.push_back(Col());
}

size_t HellModulator::queueChar(char c)
{
    if (c == '\n' || c == '\r' || c == '\t')
        c = ' ';
    std::vector<uint8_t> cols;
    if (!hell::charColumns(c, cols))
        return 0;
    for (int x = 0; x < hell::kCharCols; ++x)
    {
        Col col;
        col.counted = true;
        for (int y = 0; y < hell::kRows; ++y)
            if (cols[size_t(x * hell::kRows + y)])
                col.bits |= uint16_t(1u << y);
        m_q.push_back(col);
        ++m_queued;
    }
    return m_queued;
}

void HellModulator::tail(double sec)
{
    const int n = std::max(1, int(std::lround(sec * hell::kPixelRate / hell::kRows)));
    for (int i = 0; i < n; ++i)
        m_q.push_back(Col());
}

bool HellModulator::idle() const
{
    return m_q.empty() && (!m_inCol || m_filler);
}

void HellModulator::render(float *out, size_t n, double fs)
{
    if (fs <= 0.0)
    {
        std::fill(out, out + n, 0.0f);
        return;
    }
    const double dpx = hell::kPixelRate / fs; // half-pixels per sample
    const double dphi = 2.0 * M_PI * m_carrierHz / fs;
    for (size_t i = 0; i < n; ++i)
    {
        if (!m_inCol)
        {
            if (!m_q.empty())
            {
                m_col = m_q.front();
                m_q.pop_front();
                m_filler = false;
            }
            else
            {
                m_col = Col(); // idle while keyed: blank (carrier off)
                m_filler = true;
            }
            m_inCol = true;
            m_curPx = -1;
        }
        const int px = std::min(int(m_px), hell::kRows - 1);
        if (px != m_curPx)
        {
            m_curPx = px;
            m_prevTarget = m_curTarget;
            m_curTarget = (m_col.bits >> px) & 1u ? 1.0 : 0.0;
        }
        // Raised-cosine edge across the first half-pixel of a change keeps the
        // keying clicks down (about 250 Hz occupied bandwidth).
        const double f = m_px - std::floor(m_px);
        const double env = m_prevTarget + (m_curTarget - m_prevTarget) * 0.5 * (1.0 - std::cos(M_PI * f));
        out[i] = float(env * std::cos(m_phase));
        m_phase += dphi;
        if (m_phase > 2.0 * M_PI)
            m_phase -= 2.0 * M_PI;
        m_px += dpx;
        if (m_px >= double(hell::kRows))
        {
            if (m_col.counted)
                ++m_sent;
            m_inCol = false;
            m_px -= double(hell::kRows); // keep the fraction: exact average rate
            // Keep the envelope continuous into the next column.
            m_prevTarget = m_curTarget;
        }
    }
}

// ---------------------------------------------------------------------------

void HellDemodulator::setSampleRate(double fs)
{
    if (fs == m_fs)
        return;
    m_fs = fs;
    rebuild();
}

void HellDemodulator::rebuild()
{
    if (m_fs <= 0.0)
        return;
    // Hann window two half-pixels long: passes the keying (~ +/-250 Hz) and
    // little else.
    const size_t len = std::max<size_t>(8, size_t(std::lround(2.0 * m_fs / hell::kPixelRate)));
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
    reset();
}

void HellDemodulator::reset()
{
    std::fill(m_buf.begin(), m_buf.end(), std::complex<double>(0.0, 0.0));
    m_pos = 0;
    m_filled = 0;
    m_next = 0.0;
    m_peak = 1e-6;
    m_floor = 0.0;
    m_col.clear();
}

void HellDemodulator::process(const float *x, size_t n, std::vector<float> &columns)
{
    if (m_fs <= 0.0 || m_buf.empty())
        return;
    const size_t len = m_buf.size();
    const double step = m_fs / (hell::kPixelRate * (1.0 + m_trim / 100.0));
    const double peakDecay = std::exp(-1.0 / (hell::kPixelRate * 3.0)); // ~3 s
    for (size_t i = 0; i < n; ++i)
    {
        const double v = x[i];
        m_buf[m_pos] = std::complex<double>(v * std::cos(m_nco), -v * std::sin(m_nco));
        m_nco += 2.0 * M_PI * m_hz / m_fs;
        if (m_nco > 2.0 * M_PI)
            m_nco -= 2.0 * M_PI;
        m_pos = (m_pos + 1) % len;
        if (m_filled < len)
            ++m_filled;
        m_next -= 1.0;
        if (m_next > 0.0)
            continue;
        m_next += step;
        if (m_filled < len)
            continue;
        std::complex<double> acc(0.0, 0.0);
        size_t k = m_pos;
        for (size_t j = 0; j < len; ++j)
        {
            acc += m_buf[k] * m_win[j];
            if (++k == len)
                k = 0;
        }
        const double mag = 2.0 * std::abs(acc);
        m_mag = mag;
        m_peak = std::max(mag, m_peak * peakDecay);
        // Noise floor: falls quickly, rises slowly.
        m_floor += (mag < m_floor ? 0.05 : 0.002) * (mag - m_floor);
        const double span = std::max(m_peak - m_floor, 1e-9);
        m_col.push_back(float(std::clamp((mag - m_floor) / span, 0.0, 1.0)));
        if (m_col.size() == size_t(hell::kRows))
        {
            columns.insert(columns.end(), m_col.begin(), m_col.end());
            m_col.clear();
        }
    }
}

double HellDemodulator::levelDb() const
{
    return 20.0 * std::log10(std::max(m_peak, 1e-6));
}
