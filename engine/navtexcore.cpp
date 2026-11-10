#include "navtexcore.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdint>
#include <cstdlib>

namespace
{
// CCIR 476: 7-bit codes with exactly four ones (fldigi's tables).
// clang-format off
const unsigned char kLtrs[128] = {
    '_','_','_','_','_','_','_','_','_','_','_','_','_','_','_','_',
    '_','_','_','_','_','_','_','J','_','_','_','F','_','C','K','_',
    '_','_','_','_','_','_','_','W','_','_','_','Y','_','P','Q','_',
    '_','_','_','_','_','G','_','_','_','M','X','_','V','_','_','_',
    '_','_','_','_','_','_','_','A','_','_','_','S','_','I','U','_',
    '_','_','_','D','_','R','E','_','_','N','_','_',' ','_','_','_',
    '_','_','_','Z','_','L','_','_','_','H','_','_','\n','_','_','_',
    '_','O','B','_','T','_','_','_','\r','_','_','_','_','_','_','_'};
const unsigned char kFigs[128] = {
    '_','_','_','_','_','_','_','_','_','_','_','_','_','_','_','_',
    '_','_','_','_','_','_','_','\'','_','_','_','!','_',':','(','_',
    '_','_','_','_','_','_','_','2','_','_','_','6','_','0','1','_',
    '_','_','_','_','_','&','_','_','_','.','/','_',';','_','_','_',
    '_','_','_','_','_','_','_','-','_','_','_','\07','_','8','7','_',
    '_','_','_','$','_','4','3','_','_',',','_','_',' ','_','_','_',
    '_','_','_','"','_',')','_','_','_','#','_','_','\n','_','_','_',
    '_','9','?','_','5','_','_','_','\r','_','_','_','_','_','_','_'};
// clang-format on
constexpr int kCodeLtrs = 0x5a;
constexpr int kCodeFigs = 0x36;
constexpr int kCodeAlpha = 0x0f;
constexpr int kCodeBeta = 0x33;
constexpr int kCodeChar32 = 0x6a;
constexpr int kCodeRep = 0x66;
constexpr double kDeviation = 85.0;
constexpr double kBaud = 100.0;

bool checkBits(int v)
{
    int n = 0;
    while (v)
    {
        ++n;
        v &= v - 1;
    }
    return n == 4;
}

bool validAt(const int *pos)
{
    int n = 0;
    for (int i = 0; i < 7; ++i)
        n += pos[i] > 0;
    return n == 4;
}

int toCode(const int *pos)
{
    int code = 0;
    for (int i = 0; i < 7; ++i)
        code |= (pos[i] > 0) << i;
    return code;
}

int toChar(int code, bool figs)
{
    const unsigned char *t = figs ? kFigs : kLtrs;
    return t[code] != '_' ? t[code] : -code;
}

inline double decayavg(double avg, double v, double w)
{
    return w <= 1.0 ? v : avg + (v - avg) / w;
}
} // namespace

namespace navtex
{
std::string encode(const std::string &text, bool &figs)
{
    unsigned char ltrsTo[128] = {}, figsTo[128] = {};
    for (int c = 0; c < 128; ++c)
        if (checkBits(c))
        {
            if (kFigs[c] != '_')
                figsTo[kFigs[c]] = uint8_t(c);
            if (kLtrs[c] != '_')
                ltrsTo[kLtrs[c]] = uint8_t(c);
        }
    std::string out;
    for (char raw : text)
    {
        const int ch = std::toupper(static_cast<unsigned char>(raw)) & 0x7f;
        if (ch == '\n')
        {
            out.push_back(char(ltrsTo['\r']));
            out.push_back(char(ltrsTo['\n']));
            continue;
        }
        if (figs && figsTo[ch])
            out.push_back(char(figsTo[ch]));
        else if (!figs && ltrsTo[ch])
            out.push_back(char(ltrsTo[ch]));
        else if (figsTo[ch])
        {
            figs = true;
            out.push_back(char(kCodeFigs));
            out.push_back(char(figsTo[ch]));
        }
        else if (ltrsTo[ch])
        {
            figs = false;
            out.push_back(char(kCodeLtrs));
            out.push_back(char(ltrsTo[ch]));
        }
    }
    return out;
}

std::string withFec(const std::string &str)
{
    // rep (DX) first, the same character again as alpha (RX) five places later.
    std::string res;
    const size_t sz = str.size();
    const size_t offset = 2;
    for (size_t i = 0; i < offset; ++i)
    {
        res.push_back(char(kCodeRep));
        res.push_back(char(kCodeAlpha));
    }
    for (size_t i = 0; i < sz; ++i)
    {
        res.push_back(str[i]);
        res.push_back(i >= offset ? str[i - offset] : char(kCodeAlpha));
    }
    for (size_t i = 0; i < offset && sz >= offset; ++i)
    {
        res.push_back(char(kCodeChar32));
        res.push_back(str[sz - offset + i]);
    }
    return res;
}
} // namespace navtex

// ===========================================================================
// Decoder
// ===========================================================================

NavtexDecoder::NavtexDecoder()
{
    m_bits.assign(size_t(kBaud), 0);
}

void NavtexDecoder::setSampleRate(double fs)
{
    if (fs == m_fs)
        return;
    m_fs = fs;
    configure();
}

void NavtexDecoder::setCentreHz(double hz)
{
    m_centre = std::clamp(hz, 300.0, 2700.0);
}

void NavtexDecoder::configure()
{
    m_bitSamples = m_fs / kBaud;
    const size_t box = std::max<size_t>(4, size_t(m_bitSamples / 2));
    m_markBuf.assign(box, {0, 0});
    m_spaceBuf.assign(box, {0, 0});
    m_markSum = m_spaceSum = {0, 0};
    m_boxPos = 0;
    reset();
}

void NavtexDecoder::reset()
{
    m_state = SyncSetup;
    m_sampleCount = 0;
    m_earlyAcc = m_promptAcc = m_lateAcc = 0;
    m_nextEarly = 0;
    m_nextPrompt = m_bitSamples / 5;
    m_nextLate = m_bitSamples * 2 / 5;
    m_avgEarly = m_avgPrompt = m_avgLate = 0;
    std::fill(m_bits.begin(), m_bits.end(), 0);
    m_cursor = 0;
    m_errors = 0;
    m_shift = false;
    m_alphaPhase = false;
    m_lastChar = 0;
}

std::string NavtexDecoder::process(const float *x, size_t n)
{
    m_out.clear();
    if (m_fs <= 0.0)
        return m_out;
    const double wm = 2 * M_PI * (m_centre + kDeviation) / m_fs;
    const double ws = 2 * M_PI * (m_centre - kDeviation) / m_fs;
    const size_t box = m_markBuf.size();
    double pw = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double v = 32767.0 * x[i];
        pw += double(x[i]) * x[i];
        const std::complex<double> zm(v * std::cos(m_markPh), -v * std::sin(m_markPh));
        const std::complex<double> zs(v * std::cos(m_spacePh), -v * std::sin(m_spacePh));
        m_markPh += wm;
        m_spacePh += ws;
        if (m_markPh > 2 * M_PI)
            m_markPh -= 2 * M_PI;
        if (m_spacePh > 2 * M_PI)
            m_spacePh -= 2 * M_PI;
        m_markSum += zm - m_markBuf[m_boxPos];
        m_spaceSum += zs - m_spaceBuf[m_boxPos];
        m_markBuf[m_boxPos] = zm;
        m_spaceBuf[m_boxPos] = zs;
        if (++m_boxPos == box)
            m_boxPos = 0;
        onSample(std::abs(m_markSum) / double(box), std::abs(m_spaceSum) / double(box));
    }
    if (n)
    {
        m_level += 0.2 * (pw / double(n) - m_level);
        m_levelDb = 10.0 * std::log10(m_level + 1e-12);
    }
    return m_out;
}

void NavtexDecoder::onSample(double markAbs, double spaceAbs)
{
    multicorrelator();

    const double bs = m_bitSamples;
    const auto env = [bs](double avg, double v) { return decayavg(avg, v, v > avg ? bs / 4 : bs * 16); };
    const auto noise = [bs](double avg, double v) { return decayavg(avg, v, v < avg ? bs / 4 : bs * 48); };
    m_markEnv = env(m_markEnv, markAbs);
    m_markNoise = noise(m_markNoise, markAbs);
    m_spaceEnv = env(m_spaceEnv, spaceAbs);
    m_spaceNoise = noise(m_spaceNoise, spaceAbs);
    const double floor = (m_spaceNoise + m_markNoise) / 2;
    markAbs = std::max(std::min(markAbs, m_markEnv), floor);
    spaceAbs = std::max(std::min(spaceAbs, m_spaceEnv), floor);
    // W7AY optimal ATC
    const double logic = (markAbs - floor) * (m_markEnv - floor) - (spaceAbs - floor) * (m_spaceEnv - floor)
        - 0.5 * ((m_markEnv - floor) * (m_markEnv - floor) - (m_spaceEnv - floor) * (m_spaceEnv - floor));
    int markState = int(std::log(1 + std::fabs(logic)));
    if (logic < 0)
        markState = -markState;
    m_earlyAcc += markState;
    m_promptAcc += markState;
    m_lateAcc += markState;

    const double t = double(m_sampleCount);
    if (t >= m_nextEarly)
    {
        m_avgEarly = decayavg(m_avgEarly, std::fabs(m_earlyAcc), 64);
        m_nextEarly += bs;
        m_earlyAcc = 0;
    }
    if (t >= m_nextLate)
    {
        m_avgLate = decayavg(m_avgLate, std::fabs(m_lateAcc), 64);
        m_nextLate += bs;
        m_lateAcc = 0;
    }
    if (t >= m_nextPrompt)
    {
        m_avgPrompt = decayavg(m_avgPrompt, std::fabs(m_promptAcc), 64);
        m_nextPrompt += bs;
        int value = int(m_promptAcc);
        if (m_reverse)
            value = -value;
        m_promptAcc = 0;
        if (m_state == SyncSetup)
        {
            m_errors = 0;
            m_shift = false;
            m_state = Sync;
        }
        handleBit(value);
    }
    else if (m_state == SyncSetup)
    {
        m_errors = 0;
        m_shift = false;
        m_state = Sync;
    }
    ++m_sampleCount;
}

void NavtexDecoder::multicorrelator()
{
    const long every = long(m_bitSamples * 8);
    if (every <= 0 || m_sampleCount % every)
        return;
    double slope = m_avgLate - m_avgEarly;
    if (m_avgPrompt * 1.05 < m_avgEarly && m_avgPrompt * 1.05 < m_avgLate)
    {
        if (m_avgEarly > m_avgLate)
        {
            slope = m_nextEarly - m_nextPrompt;
            slope = std::fmod(slope - m_bitSamples, m_bitSamples);
            m_avgLate = m_avgPrompt;
            m_avgPrompt = m_avgEarly;
        }
        else
        {
            slope = m_nextLate - m_nextPrompt;
            slope = std::fmod(slope + m_bitSamples, m_bitSamples);
            m_avgEarly = m_avgPrompt;
            m_avgPrompt = m_avgLate;
        }
    }
    else
        slope /= 1024;
    if (slope != 0.0)
    {
        m_nextEarly += slope;
        m_nextPrompt += slope;
        m_nextLate += slope;
    }
}

int NavtexDecoder::findAlpha()
{
    int bestOffset = 0, bestScore = 0;
    const int limit = int(m_bits.size()) - 7;
    for (int offset = 35; offset < 35 + 14; ++offset)
    {
        int score = 0, reps = 0;
        for (int i = offset; i < limit; i += 7)
        {
            if (!validAt(&m_bits[size_t(i)]))
                continue;
            const int code = toCode(&m_bits[size_t(i)]);
            const int rep = toCode(&m_bits[size_t(i - 35)]);
            ++score;
            if (code == rep)
            {
                if (code == kCodeAlpha || code == kCodeRep)
                {
                    score = 0;
                    continue;
                }
                ++reps;
            }
            else if (code == kCodeAlpha)
            {
                if (toCode(&m_bits[size_t(i - 7)]) == kCodeRep)
                    ++reps;
            }
        }
        if (reps >= 3 && score + reps > bestScore)
        {
            bestScore = score + reps;
            bestOffset = offset;
        }
    }
    return bestScore > 8 ? bestOffset : -1;
}

void NavtexDecoder::handleBit(int value)
{
    const int size = int(m_bits.size());
    std::rotate(m_bits.begin(), m_bits.begin() + 1, m_bits.end());
    m_bits[size_t(size - 1)] = value;
    if (m_cursor > 0)
        --m_cursor;

    if (m_state == Sync)
    {
        const int offset = findAlpha();
        if (offset >= 0)
        {
            m_state = ReadData;
            m_cursor = offset;
            m_alphaPhase = true;
        }
        else
            m_state = SyncSetup;
    }
    if (m_state == ReadData && m_cursor < size - 7)
    {
        if (m_alphaPhase)
        {
            m_errors -= processBytes(m_cursor);
            if (m_errors > 5)
                m_state = SyncSetup;
            if (m_errors < 0)
                m_errors = 0;
        }
        m_alphaPhase = !m_alphaPhase;
        m_cursor += 7;
    }
}

void NavtexDecoder::flipSmallest(int *pos)
{
    int minZero = INT_MIN, minOne = INT_MAX, minZeroPos = -1, minOnePos = -1;
    int countZero = 0, countOne = 1; // as in fldigi
    for (int i = 0; i < 7; ++i)
    {
        const int v = pos[i];
        if (v < 0)
        {
            ++countZero;
            if (v > minZero)
            {
                minZero = v;
                minZeroPos = i;
            }
        }
        else
        {
            ++countOne;
            if (v < minOne)
            {
                minOne = v;
                minOnePos = i;
            }
        }
    }
    if (countZero == 4 && minZeroPos >= 0)
        pos[minZeroPos] = -pos[minZeroPos];
    else if (countOne == 5 && minOnePos >= 0)
        pos[minOnePos] = -pos[minOnePos];
}

int NavtexDecoder::processBytes(int cursor)
{
    int code = toCode(&m_bits[size_t(cursor)]);
    int success = 0;
    if (checkBits(code))
    {
        processChar(code);
        return 1;
    }
    const int repPos = cursor - 35;
    if (repPos < 0)
        return -1;
    const int rep = toCode(&m_bits[size_t(repPos)]);
    if (checkBits(rep))
    {
        if (rep == kCodeRep)
            return 0;
        processChar(rep);
        return 0;
    }
    int avg[7];
    for (int i = 0; i < 7; ++i)
        avg[i] = m_bits[size_t(cursor + i)] + m_bits[size_t(repPos + i)];
    int calc = toCode(avg);
    if (checkBits(calc))
        success = -1;
    else
    {
        flipSmallest(&m_bits[size_t(cursor)]);
        calc = toCode(&m_bits[size_t(cursor)]);
        if (!checkBits(calc))
        {
            flipSmallest(&m_bits[size_t(repPos)]);
            calc = toCode(&m_bits[size_t(repPos)]);
            if (!checkBits(calc))
            {
                flipSmallest(avg);
                calc = toCode(avg);
                if (!checkBits(calc))
                    return -2;
            }
        }
        success = -1;
    }
    processChar(calc);
    return success;
}

void NavtexDecoder::processChar(int chr)
{
    const int code = chr;
    switch (chr)
    {
    case kCodeRep:
        if (m_lastChar == kCodeRep)
            m_alphaPhase = false; // two reps in a row: fix the phase
        break;
    case kCodeAlpha:
    case kCodeBeta:
    case kCodeChar32:
        break;
    case kCodeLtrs:
        m_shift = false;
        break;
    case kCodeFigs:
        m_shift = true;
        break;
    default:
    {
        const int c = toChar(chr, m_shift);
        if (c == 0x07)
            m_out.push_back('\'');
        else if (c > 0 && c != '\r')
            m_out.push_back(char(c));
        break;
    }
    }
    m_lastChar = code;
}

// ===========================================================================
// Encoder (tests)
// ===========================================================================

void NavtexEncoder::start(const std::string &text, double phasingSec)
{
    m_bitsOut.clear();
    m_bit = 0;
    m_t = 0.0;
    const auto put = [this](int code)
    {
        for (int j = 0; j < 7; ++j)
            m_bitsOut.push_back((code >> j) & 1);
    };
    const int pairs = std::max(4, int(phasingSec * kBaud / 14.0));
    for (int i = 0; i < pairs; ++i)
    {
        put(kCodeRep);
        put(kCodeAlpha);
    }
    bool figs = false;
    const std::string codes = navtex::withFec(navtex::encode(text, figs));
    for (char c : codes)
        put(static_cast<unsigned char>(c));
    for (int i = 0; i < 10; ++i)
        put(kCodeAlpha);
}

void NavtexEncoder::render(float *out, size_t n, double fs)
{
    const double bitSec = 1.0 / kBaud;
    for (size_t i = 0; i < n; ++i)
    {
        if (done())
        {
            out[i] = 0.0f;
            continue;
        }
        const double hz = m_centre + (m_bitsOut[m_bit] ? kDeviation : -kDeviation);
        m_phase += 2 * M_PI * hz / fs;
        if (m_phase > 2 * M_PI)
            m_phase -= 2 * M_PI;
        out[i] = float(std::sin(m_phase));
        m_t += 1.0 / fs;
        if (m_t >= bitSec)
        {
            m_t -= bitSec;
            ++m_bit;
        }
    }
}
