// Qt-free test: synthetic CW through wfview-style TCI RX frames into the CW decoder.
// Build: g++ -std=c++17 -O2 -Isrc -Iengine -Iengine/ggmorse/include tests/test_cw.cpp src/tciprotocol.cpp
//        engine/ggmorse/src/ggmorse.cpp engine/ggmorse/src/resampler.cpp -o test_cw

#include "cwdecodercore.h"
#include "tciprotocol.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

static const std::map<char, const char *> kMorse
    = {{'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},     {'F', "..-."},
       {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
       {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},
       {'S', "..."},   {'T', "-"},     {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
       {'Y', "-.--"},  {'Z', "--.."},  {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
       {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."}};

static std::vector<float> keyCw(const std::string &text, double wpm, double pitch, double rate, double noise)
{
    const double dot = 1.2 / wpm;
    std::vector<bool> keyed;
    const auto add = [&](bool on, double units)
    {
        const size_t n = size_t(units * dot * rate);
        keyed.insert(keyed.end(), n, on);
    };
    add(false, 20);
    for (char c : text)
    {
        if (c == ' ')
        {
            add(false, 4); // + 3 after the previous letter = 7
            continue;
        }
        const auto it = kMorse.find(c);
        if (it == kMorse.end())
            continue;
        for (const char *p = it->second; *p; ++p)
        {
            add(true, *p == '.' ? 1 : 3);
            add(false, 1);
        }
        add(false, 2);
    }
    add(false, 40);
    std::vector<float> out(keyed.size());
    std::mt19937 rng(3);
    std::normal_distribution<float> nd(0.0f, float(noise));
    double env = 0.0;
    const double a = 1.0 - std::exp(-1.0 / (0.004 * rate)); // 4 ms rise/fall
    for (size_t i = 0; i < keyed.size(); ++i)
    {
        env += ((keyed[i] ? 1.0 : 0.0) - env) * a;
        out[i] = float(0.3 * env * std::sin(2 * M_PI * pitch * i / rate)) + nd(rng);
    }
    return out;
}

static bool run(CwDecoderCore::Engine engine, double noise)
{
    const double rate = 48000.0;
    const std::string sent = "CQ CQ DE KC9OXP KC9OXP K";
    std::vector<float> mono = keyCw(sent, 22.0, 700.0, rate, noise);
    CwDecoderCore core;
    core.setEngine(engine);
    core.setTrackRange(200.0f, 1200.0f, -1.0f);
    std::string got;
    std::vector<float> stereo(4096);
    for (size_t pos = 0; pos + 2048 <= mono.size(); pos += 2048)
    {
        for (size_t i = 0; i < 2048; ++i)
            stereo[2 * i] = stereo[2 * i + 1] = mono[pos + i];
        // through a wfview-style frame (header says 1 channel)
        std::vector<uint8_t> f = tci::makeTxAudioFrame(0, 48000, 1, stereo.data(), stereo.size());
        tci::Header h;
        tci::readHeader(f.data(), f.size(), h);
        std::vector<float> s, m;
        tci::readSamples(f.data(), f.size(), h, s);
        tci::firstChannel(s, tci::effectiveChannels(h, 2, true), m);
        std::string out;
        core.feed(m.data(), int(m.size()), float(rate), out);
        got += out;
    }
    const auto st = core.stats();
    std::printf("  %s noise %.2f: pitch %.0f Hz, %.1f WPM -> [%s]\n",
                engine == CwDecoderCore::Engine::Hand ? "Hand key" : "Machine",
                noise,
                st.pitchHz,
                st.speedWpm,
                got.c_str());
    return got.find("KC9OXP") != std::string::npos;
}

int main()
{
    int fail = 0;
    std::printf("CW through TCI frames\n");
    fail += !run(CwDecoderCore::Engine::Machine, 0.0);
    fail += !run(CwDecoderCore::Engine::Hand, 0.0);
    fail += !run(CwDecoderCore::Engine::Machine, 0.05);
    fail += !run(CwDecoderCore::Engine::Hand, 0.05);
    std::printf(fail ? "FAILED: %d\n" : "ALL PASSED\n", fail);
    return fail ? 1 : 0;
}
