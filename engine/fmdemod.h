#pragma once

// Audio-FM (frequency) demodulator for SSTV and WEFAX: mixes the audio down
// around centreHz, low-pass filters and decimates to about outRate, and
// returns the instantaneous audio frequency of each output sample in Hz
// (plus its level). Qt-free.

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <complex>
#include <cstddef>
#include <vector>

class FmDemod
{
public:
    struct Out
    {
        float hz;
        float level; // |baseband|, linear
    };

    // bandwidthHz: one-sided low-pass cut-off around centreHz.
    void configure(double sampleRate, double centreHz, double bandwidthHz, double outRate)
    {
        if (sampleRate == m_fs && centreHz == m_fc && bandwidthHz == m_bw && outRate == m_reqOut)
            return;
        m_fs = sampleRate;
        m_fc = centreHz;
        m_bw = bandwidthHz;
        m_reqOut = outRate;
        m_decim = std::max(1, int(std::lround(sampleRate / outRate)));
        m_outRate = sampleRate / m_decim;
        // Windowed-sinc low-pass, about 4 ms long.
        int taps = int(std::lround(0.004 * sampleRate)) | 1;
        m_taps.resize(size_t(taps));
        const double fc = bandwidthHz / sampleRate;
        const int mid = taps / 2;
        double sum = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const double t = i - mid;
            const double w = 0.42 + 0.5 * std::cos(M_PI * t / (mid + 1)) + 0.08 * std::cos(2 * M_PI * t / (mid + 1));
            const double s = t == 0 ? 2 * fc : std::sin(2 * M_PI * fc * t) / (M_PI * t);
            m_taps[size_t(i)] = s * w;
            sum += s * w;
        }
        for (double &v : m_taps)
            v /= sum;
        m_ring.assign(m_taps.size(), {0.0, 0.0});
        m_ringPos = 0;
        m_phase = 0.0;
        m_count = 0;
        m_prev = {0.0, 0.0};
    }
    double outRate() const
    {
        return m_outRate;
    }
    double sampleRate() const
    {
        return m_fs;
    }

    void process(const float *x, size_t n, std::vector<Out> &out)
    {
        const double dph = 2 * M_PI * m_fc / m_fs;
        const size_t nt = m_taps.size();
        for (size_t i = 0; i < n; ++i)
        {
            m_ring[m_ringPos] = std::complex<double>(x[i] * std::cos(m_phase), -x[i] * std::sin(m_phase));
            m_phase += dph;
            if (m_phase > 2 * M_PI)
                m_phase -= 2 * M_PI;
            m_ringPos = (m_ringPos + 1) % nt;
            if (++m_count < m_decim)
                continue;
            m_count = 0;
            std::complex<double> acc {0.0, 0.0};
            size_t k = m_ringPos;
            for (size_t t = 0; t < nt; ++t)
            {
                acc += m_ring[k] * m_taps[t];
                if (++k == nt)
                    k = 0;
            }
            const double d = std::arg(acc * std::conj(m_prev));
            m_prev = acc;
            out.push_back({float(m_fc + d * m_outRate / (2 * M_PI)), float(2.0 * std::abs(acc))});
        }
    }

private:
    double m_fs = 0.0, m_fc = 0.0, m_bw = 0.0, m_reqOut = 0.0, m_outRate = 0.0;
    int m_decim = 1;
    int m_count = 0;
    std::vector<double> m_taps;
    std::vector<std::complex<double>> m_ring;
    size_t m_ringPos = 0;
    double m_phase = 0.0;
    std::complex<double> m_prev {0.0, 0.0};
};
