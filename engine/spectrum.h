#pragma once

// Waterfall spectrum: Hann-windowed FFT lines of the receive audio, 0..maxHz,
// in dB. Qt-free.

#include <complex>
#include <cstddef>
#include <vector>

class SpectrumAnalyzer
{
public:
    // Bin spacing about 3 Hz whatever the sample rate; linesPerSecond lines.
    void setSampleRate(double fs, double maxHz = 3000.0, double linesPerSecond = 12.0);
    double sampleRate() const
    {
        return m_fs;
    }
    double hzPerBin() const
    {
        return m_n > 0 ? m_fs / double(m_n) : 1.0;
    }
    // Appends finished lines (each binsOut() values, dB) to lines.
    void push(const float *x, size_t n, std::vector<std::vector<float>> &lines);
    size_t binsOut() const
    {
        return m_bins;
    }

private:
    void line(std::vector<float> &out);

    double m_fs = 0.0;
    size_t m_n = 0; // FFT size
    size_t m_bins = 0;
    size_t m_hop = 1;
    size_t m_sinceLine = 0;
    std::vector<float> m_ring;
    size_t m_pos = 0;
    size_t m_filled = 0;
    std::vector<double> m_win;
    std::vector<std::complex<double>> m_work;
    std::vector<std::complex<double>> m_tw;
};

// In-place radix-2 FFT; n must be a power of two, tw = exp(-2 pi i k / n), k < n/2.
void fftRadix2(std::vector<std::complex<double>> &a, const std::vector<std::complex<double>> &tw);
