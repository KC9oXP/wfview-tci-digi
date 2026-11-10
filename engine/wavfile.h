#pragma once

// Minimal WAV reader and writer for receive recordings and playback. Qt-free.
// Reads 8/16/24/32-bit PCM and 32-bit float, any channel count (mixed to
// mono). Writes 16-bit PCM mono.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace wav
{
// Whole file into memory as mono floats. Returns false with err set on failure.
bool read(const std::string &path, std::vector<float> &mono, double &sampleRate, std::string &err);
} // namespace wav

class WavWriter
{
public:
    ~WavWriter();
    bool open(const std::string &path, int sampleRate);
    void write(const float *mono, size_t n);
    void close(); // fixes up the header
    bool isOpen() const
    {
        return m_f != nullptr;
    }
    double seconds() const
    {
        return m_rate > 0 ? double(m_frames) / m_rate : 0.0;
    }

private:
    std::FILE *m_f = nullptr;
    int m_rate = 0;
    uint64_t m_frames = 0;
};
