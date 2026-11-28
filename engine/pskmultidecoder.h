#pragma once

// Multi-signal PSK ("PSK browser"): finds every BPSK signal at one speed in
// the passband and runs one PskDemodulator per signal, so a band of PSK31
// stations is copied at once. Qt-free; single-threaded (decoder worker).
//
//   1. Audio is resampled once to 8 kHz; every channel works on that.
//   2. Spectrum: 2048-point Hann FFT every 1024 samples (3.9 Hz bins, a new
//      line every 0.128 s), smoothed. A signal is a peak of the power summed
//      over +/- 0.6 baud (both idle tones of a PSK signal) that stands
//      kPeakDb over the local noise (median of the bins within +/- 250 Hz,
//      leaving out the signal itself).
//   3. A peak with no channel near it opens one; the new demodulator is first
//      fed the last kHistorySec of audio, so the start of the over is copied.
//      AFC then keeps it on the signal. Two channels that end up on the same
//      signal are merged (the one that copied less goes).
//   4. A channel is dropped kDropSec after its signal was last seen (kTrialSec
//      if it never printed: a carrier, a splatter peak, an FT8 tone ...).
//   5. Each channel has its own squelch (the tab's setting, but never looser
//      than the defaults: with tens of channels, noise would print otherwise).

#include "audioresampler.h"
#include "pskcore.h"

#include <complex>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class PskMultiDecoder
{
public:
    struct ChannelInfo
    {
        int id = 0;
        double hz = 0.0; // after AFC
        double snrDb = 0.0; // signal over the local noise on the spectrum, dB (last seen)
        bool active = false; // signal there and squelch open now
        std::string text; // recent decoded text (tail)
        unsigned long long total = 0; // characters ever added to text
    };

    static constexpr int kMaxChannels = 24;
    static constexpr double kFs = 8000.0;
    static constexpr int kFft = 2048;
    static constexpr int kHop = 1024;
    static constexpr double kPeakDb = 5.0;
    static constexpr double kHistorySec = 2.0;
    static constexpr double kDropSec = 30.0;
    static constexpr double kTrialSec = 4.0;
    static constexpr size_t kTextKeep = 2000;
    // Rows never squelch looser than these (phase quality / dB).
    static constexpr double kMinSquelch = 0.30;
    static constexpr double kMinSquelchDb = 3.0;

    PskMultiDecoder();

    void setBaud(double baud); // a new speed starts afresh
    void setRange(double loHz, double hiHz);
    void setSquelch(double threshold, bool inDb);
    void reset();

    // Feed mono audio. Returns true when the list changed (new text, a
    // channel opened or closed).
    bool feed(const float *x, size_t n, double sampleRate);

    // Channels that have printed something, lowest frequency first.
    std::vector<ChannelInfo> channels() const;
    // Every channel being decoded, printed or not (tests).
    size_t channelCount() const
    {
        return m_channels.size();
    }
    double baud() const
    {
        return m_baud;
    }

private:
    struct Channel
    {
        int id = 0;
        PskDemodulator demod;
        double hz = 0.0;
        double silentSec = 0.0;
        double ageSec = 0.0;
        double peakDb = 0.0;
        std::string text;
        unsigned long long total = 0;
    };

    void analyse();
    void openChannel(double hz, double db);
    static void appendText(Channel &c, const std::string &s);
    double rowSquelch() const;

    double m_baud = 31.25;
    double m_lo = 200.0, m_hi = 3000.0;
    double m_squelch = 0.30;
    bool m_squelchDb = false;
    double m_inRate = 0.0;
    AudioResampler m_rs;
    std::vector<float> m_tmp;
    std::vector<float> m_hist; // last kHistorySec at kFs
    std::vector<float> m_fftIn;
    std::vector<double> m_win;
    std::vector<std::complex<double>> m_tw, m_work;
    std::vector<double> m_avg;
    bool m_avgFresh = true;
    std::vector<std::unique_ptr<Channel>> m_channels;
    int m_nextId = 0;
    bool m_changed = false;
};
