#include "spectrum.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void fftRadix2(std::vector<std::complex<double>> &a, const std::vector<std::complex<double>> &tw)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const size_t step = n / len;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; ++k)
            {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * tw[k * step];
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
    }
}

void SpectrumAnalyzer::setSampleRate(double fs, double maxHz, double linesPerSecond)
{
    if (fs <= 0.0)
        return;
    m_fs = fs;
    m_n = 1;
    while (double(m_n) < fs / 3.0)
        m_n <<= 1;
    m_bins = std::min(m_n / 2, size_t(std::ceil(maxHz / (fs / double(m_n)))) + 1);
    m_hop = std::max<size_t>(1, size_t(fs / linesPerSecond));
    m_ring.assign(m_n, 0.0f);
    m_pos = m_filled = m_sinceLine = 0;
    m_win.resize(m_n);
    double sum = 0.0;
    for (size_t i = 0; i < m_n; ++i)
    {
        m_win[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(m_n));
        sum += m_win[i];
    }
    for (double &w : m_win)
        w *= 2.0 / sum; // a full-scale sine reads 0 dB
    m_work.resize(m_n);
    m_tw.resize(m_n / 2);
    for (size_t k = 0; k < m_n / 2; ++k)
        m_tw[k] = std::polar(1.0, -2.0 * M_PI * double(k) / double(m_n));
}

void SpectrumAnalyzer::push(const float *x, size_t n, std::vector<std::vector<float>> &lines)
{
    if (m_n == 0)
        return;
    for (size_t i = 0; i < n; ++i)
    {
        m_ring[m_pos] = x[i];
        m_pos = (m_pos + 1) % m_n;
        if (m_filled < m_n)
            ++m_filled;
        if (++m_sinceLine >= m_hop && m_filled == m_n)
        {
            m_sinceLine = 0;
            lines.emplace_back();
            line(lines.back());
        }
    }
}

void SpectrumAnalyzer::line(std::vector<float> &out)
{
    size_t k = m_pos;
    for (size_t i = 0; i < m_n; ++i)
    {
        m_work[i] = std::complex<double>(m_ring[k] * m_win[i], 0.0);
        if (++k == m_n)
            k = 0;
    }
    fftRadix2(m_work, m_tw);
    out.resize(m_bins);
    for (size_t b = 0; b < m_bins; ++b)
        out[b] = float(10.0 * std::log10(std::max(std::norm(m_work[b]), 1e-14)));
}
