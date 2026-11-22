#pragma once

// RSID (Reed-Solomon ID) receive: the short identification burst fldigi and
// other programs send at the start of a transmission. It names the mode and
// marks its frequency.
//
// Adapted from fldigi's src/rsid/rsid.cxx and rsid_defs.cxx
// (Copyright (C) 2008-2012 Dave Freese W1HKJ, Stelios Bounanos M0GLD,
// John Douyere VK2ETA; GNU GPL v3 or later). The ID codes, the GF(16) table
// and the encoder are fldigi's; the receiver follows the same method.
//
// The burst: 15 tones, each 1024/11025 s long, out of 16 spaced 11025/1024 Hz
// (about 10.8 Hz). Tone 7 sits on the mode's frequency. The 15 tone numbers are
// a Reed-Solomon (15,3) code word over GF(16) of the 12-bit mode number.
//
// Receive: resample to 11025 Hz, a 2048-point FFT every 512 samples (half a
// tone), and for each start bin the strongest of the 16 tone bins. A mode is
// found when the last 30 half-tone slots spell its code word with at most one
// wrong tone. The burst is looked for both the right way up and mirrored
// (signal received on the other sideband), so the radio's sideband does not
// matter.
//
// Qt-free.

#include "audioresampler.h"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rsid
{
constexpr double kSampleRate = 11025.0;
constexpr int kSymbols = 15;
constexpr double kToneHz = kSampleRate / 1024.0; // 10.77 Hz
constexpr double kSymbolSec = 1024.0 / kSampleRate; // 92.9 ms

// What the app can do with a found mode.
enum class Target
{
    None, // not decoded by this app: only shown
    Psk,
    Rtty,
    Olivia, // Olivia or Contestia
    Hell,
    Cw,
    Eot // fldigi's end-of-transmission ID
};

struct Mode
{
    int code = 0;
    const char *name = ""; // fldigi's name, e.g. "OLIVIA_8_250"
    Target target = Target::None;
    // Target settings: PSK speed (0/1/2); RTTY baud and shift; Olivia
    // contestia, tones, bandwidth.
    int pskSpeed = 0;
    double rttyBaud = 0.0;
    int rttyShift = 0;
    bool contestia = false;
    int tones = 0;
    int bandwidth = 0;
};

// fldigi's first ID table (the escape to the second table is not followed).
const std::vector<Mode> &modes();
const Mode *modeFor(int code);
// A readable name: "Olivia 8/250", "BPSK63", "RTTY 45.45/170", "MT63_1000_LG".
std::string displayName(const Mode &m);

// The 15 tone numbers (0..15) of a mode number (fldigi's Encode).
void encode(int code, unsigned char out[kSymbols]);

// The burst as audio (for tests and demo files): 5 silent tone times, the 15
// tones with tone 7 on centreHz, 5 silent tone times. mirrored = as heard on
// the other sideband.
std::vector<float> makeBurst(int code, double centreHz, double sampleRate, bool mirrored = false, float amp = 0.5f);

struct Hit
{
    int code = 0;
    double centreHz = 0.0; // the mode's audio frequency
    bool mirrored = false; // heard mirrored (other sideband)
    int errors = 0; // wrong tones (0 or 1)
};

class Detector
{
public:
    Detector();
    void setSampleRate(double fs);
    // Only bursts centred between these audio frequencies are reported.
    void setRange(double lowHz, double highHz);
    void reset();
    // Feeds audio; returns the IDs found in it (usually none).
    std::vector<Hit> process(const float *x, size_t n);

private:
    void analyse();
    bool search(Hit &hit);

    AudioResampler m_rs;
    double m_fs = 0.0;
    double m_lowHz = 200.0;
    double m_highHz = 3000.0;
    std::vector<float> m_in; // resampled audio not yet analysed
    std::vector<float> m_frame; // last 2048 samples
    size_t m_frameFill = 0;
    std::vector<double> m_win;
    std::vector<std::complex<double>> m_fft;
    std::vector<std::complex<double>> m_tw;
    std::vector<double> m_amp;
    // m_buckets[t][bin]: strongest tone (0..15) of the 16 starting at bin, for
    // the last 30 half-tone slots (t = 29 newest).
    std::vector<std::vector<uint8_t>> m_buckets;
    int m_slots = 0; // slots analysed since reset (a burst needs 30)
    std::vector<std::vector<unsigned char>> m_codes; // per modes() entry
    std::vector<Hit> m_hits;
};
} // namespace rsid
