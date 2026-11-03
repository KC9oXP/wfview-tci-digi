#include "tciprotocol.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace tci
{

namespace
{

std::string trim(const std::string &s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

uint32_t rd32(const uint8_t *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void wr32(uint8_t *p, uint32_t v)
{
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

size_t bytesPerSample(uint32_t format)
{
    switch (static_cast<SampleType>(format))
    {
    case SampleType::Int16:
        return 2;
    case SampleType::Int24:
        return 3;
    case SampleType::Int32:
    case SampleType::Float32:
        return 4;
    case SampleType::Float64:
        return 8;
    }
    return 0;
}

} // namespace

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string toUpper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return s;
}

bool toBool(const std::string &s)
{
    const std::string l = toLower(trim(s));
    return l == "true" || l == "1" || l == "on";
}

std::vector<Command> parseCommands(const std::string &text)
{
    std::vector<Command> out;
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t end = text.find(';', pos);
        if (end == std::string::npos)
            end = text.size();
        const std::string item = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (item.empty())
            continue;

        Command c;
        const size_t colon = item.find(':');
        c.name = toLower(trim(item.substr(0, colon)));
        if (colon != std::string::npos)
        {
            const std::string rest = item.substr(colon + 1);
            size_t a = 0;
            while (true)
            {
                const size_t comma = rest.find(',', a);
                c.args.push_back(trim(rest.substr(a, comma == std::string::npos ? std::string::npos : comma - a)));
                if (comma == std::string::npos)
                    break;
                a = comma + 1;
            }
        }
        if (!c.name.empty())
            out.push_back(std::move(c));
    }
    return out;
}

std::string makeCommand(const std::string &name, const std::vector<std::string> &args)
{
    std::string s = name;
    for (size_t i = 0; i < args.size(); ++i)
    {
        s += (i == 0) ? ':' : ',';
        s += args[i];
    }
    s += ';';
    return s;
}

bool readHeader(const uint8_t *data, size_t size, Header &h)
{
    if (!data || size < kHeaderBytes)
        return false;
    h.receiver = rd32(data + 0);
    h.sampleRate = rd32(data + 4);
    h.format = rd32(data + 8);
    h.codec = rd32(data + 12);
    h.crc = rd32(data + 16);
    h.length = rd32(data + 20);
    h.type = rd32(data + 24);
    h.channels = rd32(data + 28);
    return true;
}

bool readSamples(const uint8_t *data, size_t size, const Header &h, std::vector<float> &out)
{
    out.clear();
    const size_t bps = bytesPerSample(h.format);
    if (bps == 0 || size < kHeaderBytes)
        return false;
    size_t n = h.length;
    const size_t avail = (size - kHeaderBytes) / bps;
    if (n > avail)
        return false;
    out.resize(n);
    const uint8_t *p = data + kHeaderBytes;
    switch (static_cast<SampleType>(h.format))
    {
    case SampleType::Float32:
        for (size_t i = 0; i < n; ++i)
        {
            uint32_t u = rd32(p + 4 * i);
            float f;
            std::memcpy(&f, &u, 4);
            out[i] = f;
        }
        break;
    case SampleType::Float64:
        for (size_t i = 0; i < n; ++i)
        {
            uint64_t u = uint64_t(rd32(p + 8 * i)) | (uint64_t(rd32(p + 8 * i + 4)) << 32);
            double d;
            std::memcpy(&d, &u, 8);
            out[i] = static_cast<float>(d);
        }
        break;
    case SampleType::Int16:
        for (size_t i = 0; i < n; ++i)
        {
            const int16_t v = int16_t(uint16_t(p[2 * i]) | (uint16_t(p[2 * i + 1]) << 8));
            out[i] = v / 32768.0f;
        }
        break;
    case SampleType::Int24:
        for (size_t i = 0; i < n; ++i)
        {
            int32_t v = int32_t(uint32_t(p[3 * i]) | (uint32_t(p[3 * i + 1]) << 8) | (uint32_t(p[3 * i + 2]) << 16));
            if (v & 0x800000)
                v -= 0x1000000;
            out[i] = v / 8388608.0f;
        }
        break;
    case SampleType::Int32:
        for (size_t i = 0; i < n; ++i)
            out[i] = static_cast<float>(int32_t(rd32(p + 4 * i)) / 2147483648.0);
        break;
    }
    return true;
}

int effectiveChannels(const Header &h, int announcedChannels, bool preferAnnounced)
{
    if (preferAnnounced && announcedChannels >= 1 && announcedChannels <= 2)
        return announcedChannels;
    if (h.channels >= 1 && h.channels <= 2)
        return int(h.channels);
    if (announcedChannels >= 1 && announcedChannels <= 2)
        return announcedChannels;
    return 1;
}

void firstChannel(const std::vector<float> &interleaved, int channels, std::vector<float> &mono)
{
    if (channels <= 1)
    {
        mono = interleaved;
        return;
    }
    const size_t frames = interleaved.size() / size_t(channels);
    mono.resize(frames);
    for (size_t i = 0; i < frames; ++i)
        mono[i] = interleaved[i * size_t(channels)];
}

std::vector<uint8_t> makeTxAudioFrame(
    uint32_t receiver, uint32_t sampleRate, uint32_t channels, const float *samples, size_t sampleCount)
{
    std::vector<uint8_t> f(kHeaderBytes + sampleCount * 4, 0);
    wr32(f.data() + 0, receiver);
    wr32(f.data() + 4, sampleRate);
    wr32(f.data() + 8, uint32_t(SampleType::Float32));
    wr32(f.data() + 12, 0);
    wr32(f.data() + 16, 0);
    wr32(f.data() + 20, uint32_t(sampleCount));
    wr32(f.data() + 24, uint32_t(StreamType::TxAudio));
    wr32(f.data() + 28, channels);
    for (size_t i = 0; i < sampleCount; ++i)
    {
        uint32_t u;
        std::memcpy(&u, &samples[i], 4);
        wr32(f.data() + kHeaderBytes + 4 * i, u);
    }
    return f;
}

} // namespace tci
