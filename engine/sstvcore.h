#pragma once

// SSTV (slow-scan television) receive and transmit. Qt-free.
//
// Modes: Martin M1/M2, Scottie S1/S2/DX, Robot 36/72, PD50/90/120/160/180/240.
// Timings follow the published mode specifications (JL Barber, N7CXI); VIS
// codes and the YCrCb scaling match QSSTV and MMSSTV.
//
// Receive:
//   audio -> FmDemod (instantaneous frequency, 12 kHz) -> frequency track
//   idle: VIS header detector (1900 leader, 1200 break/start/stop, 1100/1300 bits)
//   and, for a picture joined late, a sync detector: when the 1200 Hz sync
//   pulses repeat at one mode's line period the picture starts there
//   ("no VIS"), as MMSSTV does.
//   Tuning: a manual offset (the whole signal shifted by the radio's dial or
//   passband tuning) plus an automatic one measured on the 1900 Hz VIS
//   leader and on the 1200 Hz sync pulses.
//   receiving: pixels sampled from the track at the mode's timing; the sync
//   pulse of every line is located and a straight line fitted through them
//   (start time and line period), so a slanted or shifted picture is
//   straightened and redrawn as it comes in.
//
// Transmit: VIS header then the lines, phase-continuous FM, 1500 Hz black to
// 2300 Hz white, 1200 Hz sync.

#include "fmdemod.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sstv
{
enum Mode
{
    Martin1,
    Martin2,
    Scottie1,
    Scottie2,
    ScottieDX,
    Robot36,
    Robot72,
    PD50,
    PD90,
    PD120,
    PD160,
    PD180,
    PD240,
    ModeCount
};

enum class Family
{
    Martin,
    Scottie,
    Robot36,
    Robot72,
    PD
};

struct ModeInfo
{
    Mode mode;
    const char *name;
    int vis; // 7-bit VIS code
    Family family;
    int width;
    int height; // image lines
    double syncMs;
    double scanMs; // one colour (or Y) component
    double lineMs; // one sync period (two image lines in PD)
    int linesPerPeriod; // 2 for PD, else 1
    int periods() const
    {
        return height / linesPerPeriod;
    }
};

constexpr double kBlackHz = 1500.0;
constexpr double kWhiteHz = 2300.0;
constexpr double kSyncHz = 1200.0;

const ModeInfo &info(int mode);
int modeForVis(int vis); // -1 if unknown
double imageSeconds(int mode); // without the VIS header
// How far (in pixels) the last line ends up shifted against the first when the
// picture's timing is off by ppm: what a sound card clock error does to a picture.
double leanPixels(int mode, double ppm);
// Where the sync pulse starts within a sync period, ms.
double syncOffsetMs(int mode);
} // namespace sstv

class SstvDecoder
{
public:
    enum class State
    {
        Idle, // waiting for a VIS header
        Receiving,
    };

    struct Events
    {
        bool started = false; // a new picture began (mode())
        bool finished = false; // the picture ended (complete or signal lost)
        bool redraw = false; // timing corrected: every line so far changed
        int dirtyFrom = -1; // image lines [dirtyFrom, dirtyTo) changed
        int dirtyTo = -1;
        bool noVis = false; // started by the sync detector (no VIS header)
    };

    SstvDecoder();

    void setSampleRate(double fs);
    // Manual slant trim, parts per million of the line period (sound-card clock error).
    void setSlantPpm(double ppm)
    {
        m_slantPpm = ppm;
    }
    void setAutoSlant(bool on)
    {
        m_autoSlant = on;
    }
    // Signal offset: received tones are this many Hz above nominal (sync at
    // 1200 + offset). The automatic part is added when autoOffset is on.
    // Setting a new manual offset drops the automatic correction: the
    // operator has said where the signal is (auto then refines from there).
    void setOffsetHz(double hz)
    {
        if (hz != m_manualOff)
            m_autoOff = 0.0;
        m_manualOff = hz;
    }
    void clearAutoOffset()
    {
        m_autoOff = 0.0;
    }
    void setAutoOffset(bool on)
    {
        m_autoOffOn = on;
        if (!on)
            m_autoOff = 0.0;
    }
    double offsetHz() const
    {
        return m_manualOff + m_autoOff;
    }
    double autoOffsetHz() const
    {
        return m_autoOff;
    }
    // Start pictures from their sync pulses when the VIS header was missed.
    void setAutoStart(bool on)
    {
        m_autoStart = on;
        m_pulses.clear();
    }
    void reset(); // back to Idle, picture dropped

    // Start receiving a picture now in the given mode (no VIS seen). It runs for
    // the mode's whole length: only the last line, stop(), reset() or a new VIS
    // header end it. The line phase is searched over the whole sync period until
    // three syncs agree, and searched again if it is lost.
    void startManual(int mode);
    // Stop the current picture (keeps what was received).
    void stop();

    Events process(const float *x, size_t n);

    State state() const
    {
        return m_state;
    }
    int mode() const
    {
        return m_mode;
    }
    int width() const;
    int height() const;
    int linesDone() const; // image lines decoded so far
    const std::vector<uint32_t> &image() const
    {
        return m_img; // 0x00RRGGBB, width() * height()
    }
    // Line period found by the slant fit, relative to nominal (1.0 = exact).
    double periodRatio() const;
    double levelDb() const
    {
        return m_levelDb;
    }
    // Last frequency seen (for a tuning indicator), Hz.
    double lastHz() const
    {
        return m_lastHz;
    }
    double internalRate() const
    {
        return m_rate;
    }

private:
    void onBin(Events &ev);
    void begin(int mode, double t0, Events &ev, bool periodStart = false);
    float val(double t) const; // offset-corrected frequency, clipped to 1000..2500
    void detectPulse(float rawHz, Events &ev);
    void trySyncStart(Events &ev);
    void renderPeriod(int p);
    void measureSync(int p);
    bool fitTiming();
    float trackAt(double t) const; // linear interpolation, absolute sample index
    float pixel(double t, double durSamples) const;
    void trimTrack();
    double nominalPeriod() const;

    FmDemod m_demod;
    double m_fs = 0.0;
    double m_rate = 12000.0;
    double m_slantPpm = 0.0;
    bool m_autoSlant = true;
    State m_state = State::Idle;

    std::vector<float> m_track; // frequency, Hz
    double m_base = 0.0; // absolute sample index of m_track[0]
    double m_now = 0.0; // absolute index of the next sample
    std::vector<FmDemod::Out> m_tmp;

    // VIS detection: 5 ms bin means
    std::vector<float> m_bins;
    double m_binAcc = 0.0;
    int m_binCount = 0;
    double m_binStart = 0.0; // absolute index where the current bin began
    std::vector<double> m_binStarts;

    // picture
    int m_mode = -1;
    double m_t0 = 0.0; // absolute index of the start of sync period 0
    double m_period = 0.0; // samples per sync period
    int m_periodsDone = 0;
    int m_noSyncRun = 0;
    bool m_manual = false; // started by hand: runs the whole mode, never ends on "signal gone"
    bool m_phaseLocked = false; // manual start: line phase confirmed by agreeing syncs
    int m_wideAgree = 0; // manual start: wide-search hits in a row at the same phase
    std::vector<std::pair<int, double>> m_syncs; // period, absolute sync start
    std::vector<uint32_t> m_img;
    std::vector<uint8_t> m_cr, m_cb; // Robot 36 chroma per line

    double m_level = 0.0;
    double m_levelDb = -120.0;
    double m_lastHz = 0.0;

    // tuning
    double m_manualOff = 0.0;
    double m_autoOff = 0.0;
    bool m_autoOffOn = true;

    // sync detector (Idle)
    struct Pulse
    {
        double t; // absolute index of the start
        double width; // samples
        double hz; // mean raw frequency
    };
    bool m_autoStart = true;
    std::vector<float> m_smooth; // ~1 ms box filter of the raw track
    size_t m_smoothPos = 0;
    double m_smoothSum = 0.0;
    bool m_inPulse = false;
    double m_pulseStart = 0.0;
    double m_pulseHz = 0.0;
    int m_pulseN = 0;
    std::vector<Pulse> m_pulses;
};

class SstvEncoder
{
public:
    // rgb: width * height of the mode, 0x00RRGGBB. Sends the VIS header first.
    void start(int mode, const std::vector<uint32_t> &rgb);
    void clear();
    bool active() const
    {
        return m_mode >= 0 && !m_done;
    }
    bool done() const
    {
        return m_done;
    }
    double progress() const; // 0..1
    // Image line being sent now: -1 during the VIS header (or idle), height when done.
    // For PD modes (two lines per sync period) it is the first line of the pair.
    int lineSending() const;
    int mode() const
    {
        return m_mode;
    }
    double totalSeconds() const
    {
        return m_total;
    }
    // Renders mono samples (amplitude 1); silence when finished.
    void render(float *out, size_t n, double sampleRate);
    // Shifts every tone (tests: a mistuned station).
    void setOffsetHz(double hz)
    {
        m_offset = hz;
    }
    // Sound card clock correction, ppm: positive = every part of the picture is sent that
    // much longer (in samples), for an output clock that runs that much fast.
    void setTimingPpm(double ppm)
    {
        m_timingPpm = ppm;
    }

private:
    struct Seg
    {
        float hz;
        double ms;
    };
    void nextSegments();
    void lineYcc(int y, std::vector<float> &yv, std::vector<float> &cr, std::vector<float> &cb) const;

    int m_mode = -1;
    std::vector<uint32_t> m_rgb;
    std::vector<Seg> m_segs;
    size_t m_segIdx = 0;
    int m_stage = 0; // 0 = header, 1.. = sync periods
    double m_segEnd = 0.0; // seconds
    double m_t = 0.0; // seconds rendered
    double m_phase = 0.0;
    double m_total = 0.0;
    bool m_done = false;
    double m_offset = 0.0;
    double m_timingPpm = 0.0;
};

namespace sstv
{
// Audio frequency for a pixel value 0..255 (1500 Hz black .. 2300 Hz white).
double valueHz(double v);
} // namespace sstv
