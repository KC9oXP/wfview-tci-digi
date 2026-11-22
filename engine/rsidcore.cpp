#include "rsidcore.h"

#include "spectrum.h" // fftRadix2

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rsid
{
namespace
{
// GF(16) multiplication table and generator indices, from fldigi's rsid.cxx.
const int kSquares[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2, 3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
    13, 14, 15, 0,  2,  4,  6,  8,  10, 12, 14, 9,  11, 13, 15, 1,  3,  5,  7, 0,  3,  6,  5,  12, 15, 10, 9,  1,  2,
    7,  4,  13, 14, 11, 8,  0,  4,  8,  12, 9,  13, 1,  5,  11, 15, 3,  7,  2, 6,  10, 14, 0,  5,  10, 15, 13, 8,  7,
    2,  3,  6,  9,  12, 14, 11, 4,  1,  0,  6,  12, 10, 1,  7,  13, 11, 2,  4, 14, 8,  3,  5,  15, 9,  0,  7,  14, 9,
    5,  2,  11, 12, 10, 13, 4,  3,  15, 8,  1,  6,  0,  8,  9,  1,  11, 3,  2, 10, 15, 7,  6,  14, 4,  12, 13, 5,  0,
    9,  11, 2,  15, 6,  4,  13, 7,  14, 12, 5,  8,  1,  3,  10, 0,  10, 13, 7, 3,  9,  14, 4,  6,  12, 11, 1,  5,  15,
    8,  2,  0,  11, 15, 4,  7,  12, 8,  3,  14, 5,  1,  10, 9,  2,  6,  13, 0, 12, 1,  13, 2,  14, 3,  15, 4,  8,  5,
    9,  6,  10, 7,  11, 0,  13, 3,  14, 6,  11, 5,  8,  12, 1,  15, 2,  10, 7, 9,  4,  0,  14, 5,  11, 10, 4,  15, 1,
    13, 3,  8,  6,  7,  9,  2,  12, 0,  15, 7,  8,  14, 1,  9,  6,  5,  10, 2, 13, 11, 4,  12, 3,
};
const int kIndices[12] = {2, 4, 8, 9, 11, 15, 7, 14, 5, 10, 13, 3};

constexpr int kFftSize = 2048; // samples analysed (two tone times)
constexpr int kStep = 512; // new samples per analysis (half a tone time)
constexpr int kBins = kFftSize / 2;
constexpr int kTimes = 2 * kSymbols; // half-tone slots spanned by a burst
constexpr double kBinHz = kSampleRate / kFftSize; // 5.38 Hz: two bins per tone
constexpr int kMaxErrors = 1; // fldigi's default ("medium")
} // namespace

const std::vector<Mode> &modes()
{
    // fldigi rsid_defs.cxx, first table. Fields: code, name, target,
    // PSK speed, RTTY baud, RTTY shift, contestia, tones, bandwidth.
    static const std::vector<Mode> m = {
        {263, "EOT", Target::Eot},
        {1, "BPSK31", Target::Psk, 0},
        {110, "QPSK31", Target::None},
        {2, "BPSK63", Target::Psk, 1},
        {3, "QPSK63", Target::None},
        {4, "BPSK125", Target::Psk, 2},
        {5, "QPSK125", Target::None},
        {126, "BPSK250", Target::None},
        {127, "QPSK250", Target::None},
        {173, "BPSK500", Target::None},
        {183, "PSK125R", Target::None},
        {186, "PSK250R", Target::None},
        {187, "PSK500R", Target::None},
        {7, "PSKFEC31", Target::None},
        {8, "PSK10", Target::None},
        {9, "MT63_500_LG", Target::None},
        {10, "MT63_500_ST", Target::None},
        {11, "MT63_500_VST", Target::None},
        {12, "MT63_1000_LG", Target::None},
        {13, "MT63_1000_ST", Target::None},
        {14, "MT63_1000_VST", Target::None},
        {15, "MT63_2000_LG", Target::None},
        {17, "MT63_2000_ST", Target::None},
        {18, "MT63_2000_VST", Target::None},
        {19, "PSKAM10", Target::None},
        {20, "PSKAM31", Target::None},
        {21, "PSKAM50", Target::None},
        {22, "PSK63F", Target::None},
        {23, "PSK220F", Target::None},
        {24, "CHIP64", Target::None},
        {25, "CHIP128", Target::None},
        {26, "CW", Target::Cw},
        {27, "CCW_OOK_12", Target::None},
        {28, "CCW_OOK_24", Target::None},
        {29, "CCW_OOK_48", Target::None},
        {30, "CCW_FSK_12", Target::None},
        {31, "CCW_FSK_24", Target::None},
        {33, "CCW_FSK_48", Target::None},
        {34, "PACTOR1_FEC", Target::None},
        {113, "PACKET_110", Target::None},
        {35, "PACKET_300", Target::None},
        {36, "PACKET_1200", Target::None},
        {37, "RTTY_ASCII_7", Target::None},
        {38, "RTTY_ASCII_8", Target::None},
        {39, "RTTY_45", Target::Rtty, 0, 45.45, 170},
        {40, "RTTY_50", Target::Rtty, 0, 50.0, 170},
        {41, "RTTY_75", Target::Rtty, 0, 75.0, 850},
        {42, "AMTOR_FEC", Target::None},
        {43, "THROB_1", Target::None},
        {44, "THROB_2", Target::None},
        {45, "THROB_4", Target::None},
        {46, "THROBX_1", Target::None},
        {47, "THROBX_2", Target::None},
        {146, "THROBX_4", Target::None},
        {204, "CONTESTIA_4_125", Target::Olivia, 0, 0.0, 0, true, 4, 125},
        {55, "CONTESTIA_4_250", Target::Olivia, 0, 0.0, 0, true, 4, 250},
        {54, "CONTESTIA_4_500", Target::Olivia, 0, 0.0, 0, true, 4, 500},
        {255, "CONTESTIA_4_1000", Target::Olivia, 0, 0.0, 0, true, 4, 1000},
        {254, "CONTESTIA_4_2000", Target::Olivia, 0, 0.0, 0, true, 4, 2000},
        {169, "CONTESTIA_8_125", Target::Olivia, 0, 0.0, 0, true, 8, 125},
        {49, "CONTESTIA_8_250", Target::Olivia, 0, 0.0, 0, true, 8, 250},
        {52, "CONTESTIA_8_500", Target::Olivia, 0, 0.0, 0, true, 8, 500},
        {117, "CONTESTIA_8_1000", Target::Olivia, 0, 0.0, 0, true, 8, 1000},
        {247, "CONTESTIA_8_2000", Target::Olivia, 0, 0.0, 0, true, 8, 2000},
        {275, "CONTESTIA_16_250", Target::Olivia, 0, 0.0, 0, true, 16, 250},
        {50, "CONTESTIA_16_500", Target::Olivia, 0, 0.0, 0, true, 16, 500},
        {53, "CONTESTIA_16_1000", Target::Olivia, 0, 0.0, 0, true, 16, 1000},
        {259, "CONTESTIA_16_2000", Target::Olivia, 0, 0.0, 0, true, 16, 2000},
        {51, "CONTESTIA_32_1000", Target::Olivia, 0, 0.0, 0, true, 32, 1000},
        {201, "CONTESTIA_32_2000", Target::Olivia, 0, 0.0, 0, true, 32, 2000},
        {194, "CONTESTIA_64_500", Target::Olivia, 0, 0.0, 0, true, 64, 500},
        {193, "CONTESTIA_64_1000", Target::Olivia, 0, 0.0, 0, true, 64, 1000},
        {191, "CONTESTIA_64_2000", Target::Olivia, 0, 0.0, 0, true, 64, 2000},
        {56, "VOICE", Target::None},
        {60, "MFSK8", Target::None},
        {57, "MFSK16", Target::None},
        {147, "MFSK32", Target::None},
        {148, "MFSK11", Target::None},
        {152, "MFSK22", Target::None},
        {61, "RTTYM_8_250", Target::None},
        {62, "RTTYM_16_500", Target::None},
        {63, "RTTYM_32_1000", Target::None},
        {65, "RTTYM_8_500", Target::None},
        {66, "RTTYM_16_1000", Target::None},
        {67, "RTTYM_4_500", Target::None},
        {68, "RTTYM_4_250", Target::None},
        {119, "RTTYM_8_1000", Target::None},
        {170, "RTTYM_8_125", Target::None},
        {203, "OLIVIA_4_125", Target::Olivia, 0, 0.0, 0, false, 4, 125},
        {75, "OLIVIA_4_250", Target::Olivia, 0, 0.0, 0, false, 4, 250},
        {74, "OLIVIA_4_500", Target::Olivia, 0, 0.0, 0, false, 4, 500},
        {229, "OLIVIA_4_1000", Target::Olivia, 0, 0.0, 0, false, 4, 1000},
        {238, "OLIVIA_4_2000", Target::Olivia, 0, 0.0, 0, false, 4, 2000},
        {163, "OLIVIA_8_125", Target::Olivia, 0, 0.0, 0, false, 8, 125},
        {69, "OLIVIA_8_250", Target::Olivia, 0, 0.0, 0, false, 8, 250},
        {72, "OLIVIA_8_500", Target::Olivia, 0, 0.0, 0, false, 8, 500},
        {116, "OLIVIA_8_1000", Target::Olivia, 0, 0.0, 0, false, 8, 1000},
        {214, "OLIVIA_8_2000", Target::Olivia, 0, 0.0, 0, false, 8, 2000},
        {70, "OLIVIA_16_500", Target::Olivia, 0, 0.0, 0, false, 16, 500},
        {73, "OLIVIA_16_1000", Target::Olivia, 0, 0.0, 0, false, 16, 1000},
        {234, "OLIVIA_16_2000", Target::Olivia, 0, 0.0, 0, false, 16, 2000},
        {71, "OLIVIA_32_1000", Target::Olivia, 0, 0.0, 0, false, 32, 1000},
        {221, "OLIVIA_32_2000", Target::Olivia, 0, 0.0, 0, false, 32, 2000},
        {211, "OLIVIA_64_2000", Target::Olivia, 0, 0.0, 0, false, 64, 2000},
        {76, "PAX", Target::None},
        {77, "PAX2", Target::None},
        {78, "DOMINOF", Target::None},
        {79, "FAX", Target::None},
        {81, "SSTV", Target::None},
        {84, "DOMINOEX_4", Target::None},
        {85, "DOMINOEX_5", Target::None},
        {86, "DOMINOEX_8", Target::None},
        {87, "DOMINOEX_11", Target::None},
        {88, "DOMINOEX_16", Target::None},
        {90, "DOMINOEX_22", Target::None},
        {92, "DOMINOEX_4_FEC", Target::None},
        {93, "DOMINOEX_5_FEC", Target::None},
        {97, "DOMINOEX_8_FEC", Target::None},
        {98, "DOMINOEX_11_FEC", Target::None},
        {99, "DOMINOEX_16_FEC", Target::None},
        {101, "DOMINOEX_22_FEC", Target::None},
        {104, "FELD_HELL", Target::Hell},
        {105, "PSK_HELL", Target::None},
        {106, "HELL_80", Target::None},
        {107, "FM_HELL_105", Target::None},
        {108, "FM_HELL_245", Target::None},
        {114, "MODE_141A", Target::None},
        {123, "DTMF", Target::None},
        {125, "ALE400", Target::None},
        {131, "FDMDV", Target::None},
        {132, "JT65_A", Target::None},
        {134, "JT65_B", Target::None},
        {135, "JT65_C", Target::None},
        {136, "THOR_4", Target::None},
        {137, "THOR_8", Target::None},
        {138, "THOR_16", Target::None},
        {139, "THOR_5", Target::None},
        {143, "THOR_11", Target::None},
        {145, "THOR_22", Target::None},
        {153, "CALL_ID", Target::None},
        {155, "PACKET_PSK1200", Target::None},
        {156, "PACKET_PSK250", Target::None},
        {159, "PACKET_PSK63", Target::None},
        {172, "MODE_188_110A_8N1", Target::None},
    };
    return m;
}

const Mode *modeFor(int code)
{
    for (const Mode &m : modes())
        if (m.code == code)
            return &m;
    return nullptr;
}

std::string displayName(const Mode &m)
{
    char buf[64];
    switch (m.target)
    {
    case Target::Psk:
    case Target::None:
    case Target::Cw:
    case Target::Eot:
        break;
    case Target::Rtty:
        std::snprintf(buf, sizeof buf, "RTTY %g/%d", m.rttyBaud, m.rttyShift);
        return buf;
    case Target::Olivia:
        std::snprintf(buf, sizeof buf, "%s %d/%d", m.contestia ? "Contestia" : "Olivia", m.tones, m.bandwidth);
        return buf;
    case Target::Hell:
        return "Feld Hell";
    }
    if (m.target == Target::Eot)
        return "end of transmission";
    return m.name;
}

void encode(int code, unsigned char rs[kSymbols])
{
    rs[0] = (unsigned char)((code >> 8) & 0x0f);
    rs[1] = (unsigned char)((code >> 4) & 0x0f);
    rs[2] = (unsigned char)(code & 0x0f);
    for (int i = 3; i < kSymbols; ++i)
        rs[i] = 0;
    for (int i = 0; i < 12; ++i)
    {
        for (int j = kSymbols - 1; j > 0; --j)
            rs[j] = (unsigned char)(rs[j - 1] ^ kSquares[(rs[j] << 4) + kIndices[i]]);
        rs[0] = (unsigned char)kSquares[(rs[0] << 4) + kIndices[i]];
    }
}

std::vector<float> makeBurst(int code, double centreHz, double fs, bool mirrored, float amp)
{
    unsigned char rs[kSymbols];
    encode(code, rs);
    const size_t symLen = size_t(std::floor(kSymbolSec * fs));
    std::vector<float> out(5 * symLen, 0.0f);
    double phase = 0.0;
    for (int i = 0; i < kSymbols; ++i)
    {
        const int tone = mirrored ? 15 - rs[i] : rs[i];
        const double f = centreHz + (mirrored ? 7 - (15 - tone) : tone - 7) * kToneHz;
        // mirrored: tone k is heard at centre - (k - 7) tone steps
        const double inc = 2.0 * M_PI * f / fs;
        for (size_t j = 0; j < symLen; ++j)
        {
            phase += inc;
            if (phase > 2.0 * M_PI)
                phase -= 2.0 * M_PI;
            out.push_back(amp * float(std::sin(phase)));
        }
    }
    out.insert(out.end(), 5 * symLen, 0.0f);
    return out;
}

Detector::Detector()
{
    m_win.resize(kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        m_win[size_t(i)] = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / kFftSize); // Hamming, as fldigi
    m_tw.resize(kFftSize / 2);
    for (int k = 0; k < kFftSize / 2; ++k)
        m_tw[size_t(k)] = std::polar(1.0, -2.0 * M_PI * k / kFftSize);
    m_fft.resize(kFftSize);
    m_amp.assign(kBins, 0.0);
    m_frame.assign(kFftSize, 0.0f);
    m_buckets.assign(kTimes, std::vector<uint8_t>(kBins, 0xff));
    for (const Mode &m : modes())
    {
        std::vector<unsigned char> c(kSymbols);
        encode(m.code, c.data());
        m_codes.push_back(c);
    }
}

void Detector::setSampleRate(double fs)
{
    if (fs == m_fs)
        return;
    m_fs = fs;
    m_rs.setRates(fs, kSampleRate);
    reset();
}

void Detector::setRange(double lowHz, double highHz)
{
    m_lowHz = std::max(lowHz, 4.0 * kToneHz);
    m_highHz = std::min(highHz, kSampleRate / 2.0 - 20.0 * kToneHz);
}

void Detector::reset()
{
    m_rs.reset();
    m_in.clear();
    m_frame.assign(kFftSize, 0.0f);
    m_frameFill = 0;
    for (auto &row : m_buckets)
        std::fill(row.begin(), row.end(), uint8_t(0xff));
    m_slots = 0;
}

std::vector<Hit> Detector::process(const float *x, size_t n)
{
    m_hits.clear();
    if (m_fs <= 0.0 || !x || n == 0)
        return m_hits;
    m_rs.process(x, n, m_in);
    size_t used = 0;
    while (m_in.size() - used >= size_t(kStep))
    {
        // slide the frame by kStep
        std::move(m_frame.begin() + kStep, m_frame.end(), m_frame.begin());
        std::copy(
            m_in.begin() + std::ptrdiff_t(used), m_in.begin() + std::ptrdiff_t(used + kStep), m_frame.end() - kStep);
        used += kStep;
        m_frameFill = std::min<size_t>(kFftSize, m_frameFill + kStep);
        if (m_frameFill < size_t(kFftSize))
            continue;
        analyse();
        Hit h;
        if (search(h))
        {
            m_hits.push_back(h);
            // Start over, so the same burst is not reported again.
            for (auto &row : m_buckets)
                std::fill(row.begin(), row.end(), uint8_t(0xff));
            m_slots = 0;
        }
    }
    m_in.erase(m_in.begin(), m_in.begin() + std::ptrdiff_t(used));
    return m_hits;
}

void Detector::analyse()
{
    for (int i = 0; i < kFftSize; ++i)
        m_fft[size_t(i)] = std::complex<double>(double(m_frame[size_t(i)]) * m_win[size_t(i)], 0.0);
    fftRadix2(m_fft, m_tw);
    for (int k = 0; k < kBins; ++k)
        m_amp[size_t(k)] = std::norm(m_fft[size_t(k)]);

    // Oldest row out, a new row at the end.
    std::rotate(m_buckets.begin(), m_buckets.begin() + 1, m_buckets.end());
    std::vector<uint8_t> &row = m_buckets.back();
    std::fill(row.begin(), row.end(), uint8_t(0xff));
    const int last = kBins - kTimes - 2;
    for (int b = 1; b < last; ++b)
    {
        int best = 0;
        double bestAmp = m_amp[size_t(b)];
        for (int t = 1; t < 16; ++t)
        {
            const double a = m_amp[size_t(b + 2 * t)];
            if (a > bestAmp)
            {
                bestAmp = a;
                best = t;
            }
        }
        row[size_t(b)] = bestAmp > 0.0 ? uint8_t(best) : uint8_t(0xff);
    }
    if (m_slots < kTimes)
        ++m_slots;
}

bool Detector::search(Hit &hit)
{
    if (m_slots < kTimes)
        return false;
    // Tone 7 (the mode's frequency) is 14 bins above the start bin, or 16
    // when mirrored.
    const int lo = std::max(1, int(std::floor(m_lowHz / kBinHz)) - 16);
    const int hi = std::min(kBins - kTimes - 3, int(std::ceil(m_highHz / kBinHz)) - 14);
    int bestDist = kMaxErrors + 1;
    for (size_t c = 0; c < m_codes.size(); ++c)
    {
        const unsigned char *code = m_codes[c].data();
        for (int mir = 0; mir < 2; ++mir)
            for (int b = lo; b <= hi; ++b)
            {
                int dist = 0;
                for (int s = 0; s < kSymbols && dist < bestDist; ++s)
                {
                    const uint8_t want = uint8_t(mir ? 15 - code[s] : code[s]);
                    if (m_buckets[size_t(2 * s + 1)][size_t(b)] != want)
                        ++dist;
                }
                if (dist < bestDist)
                {
                    const double centre = (b + (mir ? 16 : 14)) * kBinHz;
                    if (centre < m_lowHz || centre > m_highHz)
                        continue;
                    bestDist = dist;
                    hit.code = modes()[c].code;
                    hit.centreHz = centre;
                    hit.mirrored = mir != 0;
                    hit.errors = dist;
                }
            }
    }
    return bestDist <= kMaxErrors;
}
} // namespace rsid
