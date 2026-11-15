#pragma once

// WWV / WWVH time-signal receiver (2.5, 5, 10, 15, 20, 25 MHz). Qt-free.
//
// From the receive audio (best in AM, so the tones do not depend on the dial):
//   - second ticks (5 ms of 1000 Hz from WWV, 1200 Hz from WWVH): their
//     spacing against the sample count gives the error of the audio clock
//     between radio and computer, in ppm
//   - the 100 Hz time code (one pulse a second: 0.2 s = 0, 0.5 s = 1,
//     0.8 s = position marker; a frame is one minute): UTC minute, hour, day
//     of year, year, DUT1, daylight-saving and leap-second flags
//   - the 440 / 500 / 600 Hz tones: how far off they read
//   - carrier mode (radio in USB or CW, dial 1 kHz below the carrier): how far
//     the carrier is from 1000 Hz, i.e. the radio's frequency error
//
// The time code has no parity, so a time counts as verified only when two
// frames in a row agree.
//
// Also an encoder that produces the signal for the tests and the mock.

#include <complex>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace wwv
{
struct Time
{
    int year = 0; // 2000..2099
    int dayOfYear = 0; // 1..366
    int hour = 0;
    int minute = 0;
    double dut1 = 0.0; // UT1 - UTC, seconds
    bool dst1 = false; // daylight time in effect at 00:00 UTC today
    bool dst2 = false; // ... at 24:00 UTC today
    bool leapWarning = false;

    bool operator==(const Time &o) const
    {
        return year == o.year && dayOfYear == o.dayOfYear && hour == o.hour && minute == o.minute;
    }
    // Minutes since 2000-01-01 00:00 (for comparing frames).
    long minutesSince2000() const;
};

// The 60 symbols of a frame: '0', '1', 'M' (marker) or '-' (no pulse, second 0).
std::string encodeFrame(const Time &t);
// False if the symbols are not a valid frame.
bool decodeFrame(const std::string &symbols, Time &t);

// Great-circle distance in km and a one-or-more-hop F-layer path delay in ms
// from a Maidenhead grid square to the station (hawaii = WWVH).
bool gridToLatLon(const std::string &grid, double &lat, double &lon);
double pathDelayMs(double lat, double lon, bool hawaii, double *distanceKm = nullptr);
} // namespace wwv

class WwvDecoder
{
public:
    struct Status
    {
        // lamps
        bool ticks = false; // second ticks found and tracked
        bool tones = false; // a 440 / 500 / 600 Hz tone is on now
        bool code = false; // 100 Hz time-code pulses seen
        bool timeValid = false; // a frame decoded
        bool timeVerified = false; // two frames in a row agreed
        bool carrier = false; // carrier mode: a steady carrier found
        bool hawaii = false; // WWVH (1200 Hz ticks) is the stronger

        double clockPpm = 0.0; // audio clock error from the ticks (+ = fast)
        double clockPpmSpanSec = 0.0; // seconds of ticks behind that figure
        double tickJitterMs = 0.0;
        int toneHz = 0; // which tone (440 / 500 / 600), 0 if none
        double toneErrorHz = 0.0;
        double carrierHz = 0.0; // carrier mode: measured audio frequency
        double carrierSeconds = 0.0; // seconds averaged
        std::string symbols; // the last symbols, newest last (for display)
        int framesDecoded = 0;

        // The time, when timeValid: 'time' is the minute of the frame that
        // second 'secondInMinute' of the latest tick belongs to, and
        // tickSample is the absolute sample index where that second began.
        wwv::Time time;
        int secondInMinute = 0;
        double tickSample = 0.0;
    };

    WwvDecoder();
    void setSampleRate(double fs);
    // Carrier mode: only the carrier near 1000 Hz is measured.
    void setCarrierMode(bool on);
    bool carrierMode() const
    {
        return m_carrierMode;
    }
    void reset();
    void process(const float *x, size_t n);
    const Status &status() const
    {
        return m_st;
    }
    double samplesProcessed() const
    {
        return double(m_n);
    }

private:
    // Complex mixer with a moving-average low-pass.
    struct Mixer
    {
        void configure(double fs, double hz, double boxSec);
        std::complex<double> step(double x);
        double hz = 0.0;
        std::complex<double> rot {1, 0}, osc {1, 0}, sum {0, 0};
        std::vector<std::complex<double>> ring;
        size_t pos = 0;
        int renorm = 0;
    };
    // Frequency from the phase advance between filtered samples lagSec apart.
    struct FreqMeter
    {
        void configure(double fs, double hz, double boxSec, double lagSec);
        void step(double x);
        // Over what was collected since the last take(): error in Hz from
        // the mixer frequency, and how coherent the tone was (0..1).
        bool take(double &errHz, double &coherence, double &level);
        Mixer mix;
        double lagSec = 0.1;
        size_t every = 1, count = 0;
        std::deque<std::complex<double>> hist;
        size_t lagSteps = 1;
        std::complex<double> acc {0, 0};
        double norm = 0.0, lvl = 0.0;
        size_t n = 0;
    };

    void onSecondEnd(long k, double t0);
    void searchFold();
    void measureTick();
    void fitTicks();
    void tryFrame();
    void updateTones();
    void updateCarrier();
    double env100At(double sample) const;
    double tickEnvAt(double sample) const;

    double m_fs = 0.0;
    uint64_t m_n = 0;
    bool m_carrierMode = false;
    Status m_st;

    // ticks
    Mixer m_t1000, m_t1200, m_t1500;
    std::vector<float> m_tickRing; // tick envelope, every sample, 3 s
    std::vector<float> m_fold; // 1 ms bins over a second
    std::vector<float> m_foldCur;
    double m_e1000 = 0.0, m_e1200 = 0.0;
    bool m_locked = false;
    long m_k = 0; // index of the second being received
    double m_slope = 0.0, m_icept = 0.0; // tick start = icept + slope * k
    std::deque<std::pair<long, double>> m_ticks;
    int m_missed = 0;
    double m_tickLevel = 0.0;
    double m_noise = 1e-6;

    // 100 Hz code
    Mixer m_m100;
    std::vector<float> m_envRing; // 100 Hz envelope, every sample, 3 s
    std::deque<char> m_sym;
    std::deque<bool> m_minuteTone;
    std::deque<bool> m_pulseSeen;
    bool m_haveFrame = false;
    wwv::Time m_frameTime;
    long m_frameK = 0; // second index of that frame's second 0

    // tones and carrier
    FreqMeter m_f440, m_f500, m_f600;
    FreqMeter m_carCoarse, m_carFine;
    double m_carCentre = 1000.0;
    double m_carSum = 0.0, m_carWeight = 0.0;
    double m_toneErr = 0.0;
    int m_toneHold = 0;
    uint64_t m_nextSlow = 0;
};

// Produces what an AM receiver hears: ticks, the 100 Hz code, the 500 / 600 Hz
// tones (and silence where the voice would be). Starts at second 'second' of
// the minute 'start'.
class WwvEncoder
{
public:
    void start(const wwv::Time &start, int second = 0, bool hawaii = false);
    void render(float *out, size_t n, double sampleRate);
    // 0 leaves the 100 Hz code out (a receiver whose audio does not pass it).
    void setCodeLevel(double v)
    {
        m_codeLevel = v;
    }

private:
    wwv::Time m_time;
    std::string m_frame;
    int m_sec = 0;
    double m_t = 0.0; // seconds into the current second
    bool m_hawaii = false;
    double m_ph100 = 0.0, m_phTick = 0.0, m_phTone = 0.0;
    double m_codeLevel = 0.25;
};
