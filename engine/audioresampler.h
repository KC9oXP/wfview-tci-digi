#pragma once

// Band-limited sample-rate converter for any pair of rates (48000 <-> 8000,
// 44100 -> 12000, ...). Windowed-sinc kernel evaluated from an oversampled
// table, cut-off at 0.45 of the lower rate. Qt-free.
//
//   AudioResampler r(48000, 8000);
//   r.process(in, n, out);   // appends to out

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstddef>
#include <vector>

class AudioResampler
{
public:
    AudioResampler() = default;
    AudioResampler(double inRate, double outRate)
    {
        setRates(inRate, outRate);
    }

    void setRates(double inRate, double outRate)
    {
        if (inRate == m_in && outRate == m_out)
            return;
        m_in = inRate;
        m_out = outRate;
        build();
    }
    double inRate() const
    {
        return m_in;
    }
    double outRate() const
    {
        return m_out;
    }

    void reset()
    {
        m_hist.assign(m_hist.size(), 0.0f);
        m_pos = 0.0;
    }

    void process(const float *x, size_t n, std::vector<float> &out)
    {
        if (m_in <= 0.0 || m_out <= 0.0)
            return;
        if (m_in == m_out)
        {
            out.insert(out.end(), x, x + n);
            return;
        }
        // m_hist holds the last 2*m_half input samples; m_pos is the time of
        // the next output sample in input samples, relative to m_hist[m_half].
        const double step = m_in / m_out;
        m_buf.resize(m_hist.size() + n);
        std::copy(m_hist.begin(), m_hist.end(), m_buf.begin());
        std::copy(x, x + n, m_buf.begin() + std::ptrdiff_t(m_hist.size()));
        const double last = double(n); // positions available: pos + m_half < hist + n
        while (m_pos < last)
        {
            const double centre = m_pos + double(m_half); // index in m_buf
            const long i0 = long(std::floor(centre)) - long(m_half) + 1;
            double acc = 0.0;
            for (long i = i0; i < i0 + long(2 * m_half); ++i)
            {
                if (i < 0 || size_t(i) >= m_buf.size())
                    continue;
                acc += double(m_buf[size_t(i)]) * kernel(double(i) - centre);
            }
            out.push_back(float(acc * m_gain));
            m_pos += step;
        }
        m_pos -= last;
        std::copy(m_buf.end() - std::ptrdiff_t(m_hist.size()), m_buf.end(), m_hist.begin());
    }

private:
    void build()
    {
        if (m_in <= 0.0 || m_out <= 0.0)
            return;
        // Kernel in units of input samples. fc relative to the input rate.
        m_fc = 0.45 * std::min(m_in, m_out) / m_in;
        m_half = size_t(std::ceil(kZeros / (2.0 * m_fc)));
        m_gain = 2.0 * m_fc;
        m_hist.assign(2 * m_half, 0.0f);
        m_pos = 0.0;
        m_table.resize(kOver * (m_half + 1) + 2);
        for (size_t k = 0; k < m_table.size(); ++k)
        {
            const double t = double(k) / kOver; // input samples from centre
            const double w = t >= double(m_half) ? 0.0 : 0.5 + 0.5 * std::cos(M_PI * t / double(m_half));
            const double a = 2.0 * M_PI * m_fc * t;
            m_table[k] = (t == 0.0 ? 1.0 : std::sin(a) / a) * w;
        }
    }
    double kernel(double t) const
    {
        t = std::fabs(t) * kOver;
        const size_t k = size_t(t);
        if (k + 1 >= m_table.size())
            return 0.0;
        const double f = t - double(k);
        return m_table[k] + f * (m_table[k + 1] - m_table[k]);
    }

    static constexpr double kZeros = 8.0; // zero crossings each side
    static constexpr int kOver = 64;
    double m_in = 0.0, m_out = 0.0;
    double m_fc = 0.5, m_gain = 1.0;
    size_t m_half = 8;
    double m_pos = 0.0;
    std::vector<float> m_hist, m_buf;
    std::vector<double> m_table;
};
