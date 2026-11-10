#include "wavfile.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
uint32_t le32(const unsigned char *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t le16(const unsigned char *p)
{
    return uint16_t(p[0] | (p[1] << 8));
}
void put32(std::FILE *f, uint32_t v)
{
    const unsigned char b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    std::fwrite(b, 1, 4, f);
}
void put16(std::FILE *f, uint16_t v)
{
    const unsigned char b[2] = {uint8_t(v), uint8_t(v >> 8)};
    std::fwrite(b, 1, 2, f);
}
std::FILE *openFile(const std::string &path, const char *mode)
{
#ifdef _WIN32
    // UTF-8 path to wide for Windows.
    std::wstring w;
    {
        int i = 0;
        const unsigned char *s = reinterpret_cast<const unsigned char *>(path.c_str());
        while (s[i])
        {
            uint32_t cp = s[i];
            int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
            if (extra)
                cp &= (0x3F >> extra);
            ++i;
            for (int k = 0; k < extra && s[i]; ++k, ++i)
                cp = (cp << 6) | (s[i] & 0x3F);
            if (cp >= 0x10000)
            {
                cp -= 0x10000;
                w.push_back(wchar_t(0xD800 + (cp >> 10)));
                w.push_back(wchar_t(0xDC00 + (cp & 0x3FF)));
            }
            else
                w.push_back(wchar_t(cp));
        }
    }
    std::wstring m(mode, mode + std::strlen(mode));
    return _wfopen(w.c_str(), m.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}
} // namespace

namespace wav
{
bool read(const std::string &path, std::vector<float> &mono, double &sampleRate, std::string &err)
{
    mono.clear();
    std::FILE *f = openFile(path, "rb");
    if (!f)
    {
        err = "cannot open the file";
        return false;
    }
    std::vector<unsigned char> data;
    {
        unsigned char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            data.insert(data.end(), buf, buf + n);
        std::fclose(f);
    }
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) || std::memcmp(data.data() + 8, "WAVE", 4))
    {
        err = "not a WAV file";
        return false;
    }
    int fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t pos = 12;
    const unsigned char *pcm = nullptr;
    size_t pcmLen = 0;
    while (pos + 8 <= data.size())
    {
        const unsigned char *h = data.data() + pos;
        const uint32_t len = le32(h + 4);
        const size_t body = pos + 8;
        if (!std::memcmp(h, "fmt ", 4) && len >= 16 && body + 16 <= data.size())
        {
            fmt = le16(data.data() + body);
            channels = le16(data.data() + body + 2);
            rate = le32(data.data() + body + 4);
            bits = le16(data.data() + body + 14);
            if (fmt == 0xFFFE && len >= 40) // WAVE_FORMAT_EXTENSIBLE: sub-format
                fmt = le16(data.data() + body + 24);
        }
        else if (!std::memcmp(h, "data", 4))
        {
            pcm = data.data() + body;
            pcmLen = std::min<size_t>(len, data.size() - body);
        }
        pos = body + len + (len & 1);
    }
    if (!pcm || channels <= 0 || rate == 0)
    {
        err = "no audio data";
        return false;
    }
    const int bytes = bits / 8;
    if (!((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32)))
    {
        err = "unsupported sample format (use PCM 8/16/24/32-bit or 32-bit float)";
        return false;
    }
    const size_t frames = pcmLen / size_t(bytes * channels);
    mono.resize(frames);
    for (size_t i = 0; i < frames; ++i)
    {
        double acc = 0.0;
        for (int c = 0; c < channels; ++c)
        {
            const unsigned char *p = pcm + (i * size_t(channels) + size_t(c)) * size_t(bytes);
            double v = 0.0;
            if (fmt == 3)
            {
                float fv;
                std::memcpy(&fv, p, 4);
                v = fv;
            }
            else if (bits == 8)
                v = (int(p[0]) - 128) / 128.0;
            else if (bits == 16)
                v = int16_t(le16(p)) / 32768.0;
            else if (bits == 24)
                v = (int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) >> 8) / 8388608.0;
            else
                v = int32_t(le32(p)) / 2147483648.0;
            acc += v;
        }
        mono[i] = float(acc / channels);
    }
    sampleRate = rate;
    return true;
}
} // namespace wav

WavWriter::~WavWriter()
{
    close();
}

bool WavWriter::open(const std::string &path, int sampleRate)
{
    close();
    m_f = openFile(path, "wb");
    if (!m_f)
        return false;
    m_rate = sampleRate;
    m_frames = 0;
    std::fwrite("RIFF", 1, 4, m_f);
    put32(m_f, 36);
    std::fwrite("WAVEfmt ", 1, 8, m_f);
    put32(m_f, 16);
    put16(m_f, 1); // PCM
    put16(m_f, 1); // mono
    put32(m_f, uint32_t(sampleRate));
    put32(m_f, uint32_t(sampleRate * 2));
    put16(m_f, 2);
    put16(m_f, 16);
    std::fwrite("data", 1, 4, m_f);
    put32(m_f, 0);
    return true;
}

void WavWriter::write(const float *mono, size_t n)
{
    if (!m_f)
        return;
    std::vector<unsigned char> buf(n * 2);
    for (size_t i = 0; i < n; ++i)
    {
        const int v = int(std::lround(std::clamp(mono[i], -1.0f, 1.0f) * 32767.0f));
        buf[2 * i] = uint8_t(v & 0xff);
        buf[2 * i + 1] = uint8_t((v >> 8) & 0xff);
    }
    std::fwrite(buf.data(), 1, buf.size(), m_f);
    m_frames += n;
}

void WavWriter::close()
{
    if (!m_f)
        return;
    const uint64_t bytes = m_frames * 2;
    std::fseek(m_f, 4, SEEK_SET);
    put32(m_f, uint32_t(std::min<uint64_t>(0xFFFFFFF0u, 36 + bytes)));
    std::fseek(m_f, 40, SEEK_SET);
    put32(m_f, uint32_t(std::min<uint64_t>(0xFFFFFFF0u, bytes)));
    std::fclose(m_f);
    m_f = nullptr;
}
