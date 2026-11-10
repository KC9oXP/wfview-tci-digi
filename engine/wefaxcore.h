#pragma once

// HF weather fax (WEFAX) receive, plus a transmitter used by the tests.
// Qt-free.
//
//   audio -> FmDemod (1500 Hz black ... 2300 Hz white) -> grey level 0..1
//   APT start tone (300 Hz black/white for IOC 576, 675 Hz for IOC 288)
//   -> phasing lines (black with a 5 % white pulse; its centre marks the
//   start of a line, and their spacing gives the true line period)
//   -> image lines at LPM lines per minute, IOC * pi pixels wide
//   -> APT stop tone (450 Hz) ends the picture.
//
// On the air: tune USB 1.9 kHz below the published frequency so the signal
// sits at 1500-2300 Hz.

#include "fmdemod.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wefax
{
inline int widthForIoc(int ioc)
{
    return int(3.14159265358979 * ioc + 0.5); // 576 -> 1810, 288 -> 905
}
} // namespace wefax

class WefaxDecoder
{
public:
    enum class State
    {
        Idle, // waiting for the APT start tone (or a manual start)
        Start, // start tone heard
        Phasing,
        Image
    };

    struct Events
    {
        bool stateChanged = false;
        bool started = false; // image lines begin
        bool finished = false; // stop tone or manual stop
        int rowsAdded = 0;
    };

    void setSampleRate(double fs);
    void setLpm(double lpm); // 60, 90, 120, 240
    void setIoc(int ioc); // 576 or 288; also sets the width
    void setSlantPpm(double ppm)
    {
        m_slantPpm = ppm;
    }
    void setAutoStart(bool on)
    {
        m_auto = on; // follow the APT tones
    }
    void reset();
    void startManual(); // straight to image lines, phase unknown
    void stop();

    Events process(const float *x, size_t n);

    State state() const
    {
        return m_state;
    }
    double lpm() const
    {
        return m_lpm;
    }
    int ioc() const
    {
        return m_ioc;
    }
    int width() const
    {
        return wefax::widthForIoc(m_ioc);
    }
    int rows() const
    {
        return int(m_img.size() / size_t(width()));
    }
    const std::vector<uint8_t> &image() const
    {
        return m_img; // grey, width() * rows()
    }
    // Line period from the phasing lines relative to nominal (1.0 = exact).
    double periodRatio() const
    {
        return m_periodRatio;
    }
    int phasingLines() const
    {
        return m_phasingLines;
    }
    double aptHz() const
    {
        return m_aptHz; // black/white alternation rate seen last
    }
    double levelDb() const
    {
        return m_levelDb;
    }

    static constexpr int kMaxRows = 4000;

private:
    void setState(State s, Events &ev);
    void aptWindow(Events &ev);
    double toneShare(double hz) const;
    void analysePhasing(Events &ev);
    void emitRows(Events &ev);
    double nominalPeriod() const;
    float at(double t) const;

    FmDemod m_demod;
    double m_fs = 0.0;
    double m_rate = 12000.0;
    double m_lpm = 120.0;
    int m_ioc = 576;
    double m_slantPpm = 0.0;
    bool m_auto = true;
    State m_state = State::Idle;

    std::vector<float> m_grey; // 0..1
    double m_base = 0.0; // absolute index of m_grey[0]
    double m_now = 0.0;
    std::vector<FmDemod::Out> m_tmp;

    // APT tone detection, 0.25 s windows
    int m_aptCount = 0;
    int m_aptTransitions = 0;
    bool m_aptHigh = false;
    std::vector<float> m_aptBuf;
    double m_aptHz = 0.0;
    int m_startRun = 0, m_notStartRun = 0, m_stopRun = 0;

    // phasing
    double m_phStart = 0.0; // absolute index of the next window to analyse
    double m_phEnteredAt = 0.0;
    std::vector<double> m_centres; // absolute pulse centres
    int m_phasingLines = 0;
    int m_nonPhasingRun = 0;

    // image
    double m_lineStart = 0.0; // absolute index where the next row begins
    double m_period = 0.0; // samples per line
    double m_periodRatio = 1.0;
    std::vector<uint8_t> m_img;

    double m_level = 0.0, m_levelDb = -120.0;
};

class WefaxEncoder
{
public:
    // grey rows of width wefax::widthForIoc(ioc). Start tone 5 s, phasing
    // lines, the picture, stop tone 5 s.
    void start(const std::vector<uint8_t> &grey, int width, double lpm, int ioc, int phasingLines = 20);
    bool done() const
    {
        return m_done;
    }
    void render(float *out, size_t n, double sampleRate);

private:
    double levelAt(double t) const; // 0..1 at time t seconds

    std::vector<uint8_t> m_grey;
    int m_w = 0, m_rows = 0;
    double m_lpm = 120.0;
    int m_ioc = 576;
    int m_phasing = 20;
    double m_t = 0.0, m_phase = 0.0, m_total = 0.0;
    bool m_done = true;
};
