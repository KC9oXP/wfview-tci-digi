// Qt-free tests: TCI protocol helpers and an RTTY loop through TCI frames.
// Build: g++ -std=c++17 -O2 -Isrc -Iengine tests/test_protocol.cpp src/tciprotocol.cpp
//        engine/rttycore.cpp engine/rttytxsource.cpp -o test_protocol

#include "rttycore.h"
#include "rttytxsource.h"
#include "tciprotocol.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                              \
            ++g_fail;                                                                                                  \
        }                                                                                                              \
    } while (0)

static void testParse()
{
    std::printf("parseCommands\n");
    auto c = tci::parseCommands("protocol:WFVIEW,1.9;device:IC-7610; vfo:0,0,14074000;ready;\r\n");
    CHECK(c.size() == 4);
    CHECK(c[0].name == "protocol" && c[0].args.size() == 2 && c[0].args[0] == "WFVIEW" && c[0].args[1] == "1.9");
    CHECK(c[1].name == "device" && c[1].args[0] == "IC-7610");
    CHECK(c[2].name == "vfo" && c[2].args.size() == 3 && c[2].args[2] == "14074000");
    CHECK(c[3].name == "ready" && c[3].args.empty());
    auto d = tci::parseCommands("TRX:0,TRUE;");
    CHECK(d.size() == 1 && d[0].name == "trx" && tci::toBool(d[0].args[1]));
    CHECK(tci::makeCommand("trx", {"0", "true"}) == "trx:0,true;");
    CHECK(tci::makeCommand("ready") == "ready;");
    auto e = tci::parseCommands(";;  ;");
    CHECK(e.empty());
}

static std::vector<uint8_t> wfviewRxFrame(const std::vector<float> &stereo)
{
    // wfview 3.00: interleaved stereo, header channels = 1, length = all floats.
    std::vector<uint8_t> f = tci::makeTxAudioFrame(0, 48000, 1, stereo.data(), stereo.size());
    const uint32_t rx = uint32_t(tci::StreamType::RxAudio);
    std::memcpy(f.data() + 24, &rx, 4); // little-endian host assumed in this test
    return f;
}

static void testFrames()
{
    std::printf("frames\n");
    std::vector<float> s = {0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f};
    auto f = tci::makeTxAudioFrame(0, 48000, 2, s.data(), s.size());
    CHECK(f.size() == tci::kHeaderBytes + 6 * 4);
    tci::Header h;
    CHECK(tci::readHeader(f.data(), f.size(), h));
    CHECK(h.sampleRate == 48000 && h.length == 6 && h.channels == 2);
    CHECK(h.type == uint32_t(tci::StreamType::TxAudio) && h.format == uint32_t(tci::SampleType::Float32));
    std::vector<float> out;
    CHECK(tci::readSamples(f.data(), f.size(), h, out) && out.size() == 6 && out[4] == 0.3f);
    std::vector<float> mono;
    tci::firstChannel(out, 2, mono);
    CHECK(mono.size() == 3 && mono[0] == 0.1f && mono[2] == 0.3f);

    // truncated frame is rejected
    CHECK(!tci::readSamples(f.data(), f.size() - 4, h, out));
    CHECK(!tci::readHeader(f.data(), 10, h));

    // wfview header bug: header 1 ch, announced 2 -> 2 when the server is wfview
    tci::Header w;
    w.channels = 1;
    CHECK(tci::effectiveChannels(w, 2, true) == 2);
    CHECK(tci::effectiveChannels(w, 2, false) == 1);
    w.channels = 0;
    CHECK(tci::effectiveChannels(w, 2, false) == 2);
    CHECK(tci::effectiveChannels(w, 0, false) == 1);

    // int16 conversion
    std::vector<uint8_t> i16(tci::kHeaderBytes + 4, 0);
    uint32_t fmt = 0, len = 2;
    std::memcpy(i16.data() + 8, &fmt, 4);
    std::memcpy(i16.data() + 20, &len, 4);
    const int16_t a = 16384, b = -32768;
    std::memcpy(i16.data() + 64, &a, 2);
    std::memcpy(i16.data() + 66, &b, 2);
    CHECK(tci::readHeader(i16.data(), i16.size(), h));
    CHECK(tci::readSamples(i16.data(), i16.size(), h, out) && std::fabs(out[0] - 0.5f) < 1e-6 && out[1] == -1.0f);
}

static void testRttyLoop(bool usb, double noise)
{
    std::printf("RTTY loop through TCI frames (%s, noise %.2f)\n", usb ? "USB" : "LSB", noise);
    RttyConfig cfg;
    cfg.sampleRate = 48000.0;
    cfg.markHz = usb ? 2295.0 : 2125.0;
    cfg.reverse = usb; // USB: space below mark (same rule as RttyEngine::config)
    RttyTxSource tx;
    tx.setConfig(cfg);
    tx.setLevel(0.5f);
    tx.start(0.3);
    const std::string msg = "CQ CQ DE KC9OXP KC9OXP K 599 73\n";
    tx.queueText(msg);
    tx.queueTail(0.3);

    RttyDemodulator demod;
    demod.setConfig(cfg);
    demod.setSquelch(0.0);

    std::mt19937 rng(7);
    std::normal_distribution<float> nd(0.0f, float(noise));
    std::string got;
    std::vector<float> stereo(2048 * 2);
    int frames = 0;
    // Emulate the server: each TX chrono asks for 2048 frames; the client
    // sends them; we loop them back as wfview-style RX frames.
    while ((!tx.idle() || frames < 20) && frames < 400)
    {
        tx.render(stereo.data(), 2048, 2, 48000.0f);
        for (float &v : stereo)
            v += nd(rng);
        auto rxf = wfviewRxFrame(stereo);
        tci::Header h;
        CHECK(tci::readHeader(rxf.data(), rxf.size(), h));
        std::vector<float> samples, mono;
        CHECK(tci::readSamples(rxf.data(), rxf.size(), h, samples));
        tci::firstChannel(samples, tci::effectiveChannels(h, 2, true), mono);
        got += demod.process(mono.data(), mono.size());
        ++frames;
    }
    std::printf("  decoded: %s", got.c_str());
    if (got.empty() || got.back() != '\n')
        std::printf("\n");
    CHECK(got.find("KC9OXP KC9OXP K 599 73") != std::string::npos);
}

static void testWrongChannelsFails()
{
    // Without the workaround the stereo stream is read as mono at half speed.
    std::printf("RTTY with the header trusted (shows why the workaround is needed)\n");
    RttyConfig cfg;
    cfg.markHz = 2295.0;
    cfg.reverse = true;
    RttyTxSource tx;
    tx.setConfig(cfg);
    tx.setLevel(0.5f);
    tx.start(0.3);
    tx.queueText("RYRYRYRY KC9OXP TEST\n");
    RttyDemodulator demod;
    demod.setConfig(cfg);
    std::string got;
    std::vector<float> stereo(4096);
    for (int i = 0; i < 80; ++i)
    {
        tx.render(stereo.data(), 2048, 2, 48000.0f);
        auto rxf = wfviewRxFrame(stereo);
        tci::Header h;
        tci::readHeader(rxf.data(), rxf.size(), h);
        std::vector<float> samples, mono;
        tci::readSamples(rxf.data(), rxf.size(), h, samples);
        tci::firstChannel(samples, tci::effectiveChannels(h, 2, false), mono);
        got += demod.process(mono.data(), mono.size());
    }
    std::printf("  decoded: [%s]\n", got.c_str());
    CHECK(got.find("KC9OXP TEST") == std::string::npos);
}

int main()
{
    testParse();
    testFrames();
    testRttyLoop(true, 0.0);
    testRttyLoop(false, 0.0);
    testRttyLoop(true, 0.15);
    testWrongChannelsFails();
    std::printf(g_fail ? "FAILED: %d\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
