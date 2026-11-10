#pragma once

// NAVTEX / SITOR-B receive (100 baud FSK, 170 Hz shift, CCIR 476 code with
// time-diversity FEC), plus an encoder for the tests. Qt-free.
//
// The bit synchroniser (early / prompt / late), the W7AY optimal-ATC slicer,
// the alpha / rep phasing search and the FEC repair follow fldigi's navtex.cxx
// (GPL, by Rik van Riel AB1KW and Remi Chateauneu F4ECW).
//
// On the air: 518 kHz (international) and 490 kHz (national) NAVTEX, tuned
// USB 1 kHz below so the signal is centred on 1000 Hz audio.

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace navtex
{
// Character to CCIR 476 codes (with LTRS/FIGS shifts); shift state carried.
std::string encode(const std::string &text, bool &figs);
// Interleave DX/RX for time diversity (each character also sent 5 later).
std::string withFec(const std::string &codes);
} // namespace navtex

class NavtexDecoder
{
public:
    NavtexDecoder();

    void setSampleRate(double fs);
    void setCentreHz(double hz); // mark = centre + 85, space = centre - 85
    void setReverse(bool on)
    {
        m_reverse = on;
    }
    void reset();

    // Returns the decoded text ('\n' line feeds; CR dropped).
    std::string process(const float *x, size_t n);

    bool locked() const
    {
        return m_state == ReadData;
    }
    double centreHz() const
    {
        return m_centre;
    }
    double levelDb() const
    {
        return m_levelDb;
    }

private:
    enum State
    {
        SyncSetup,
        Sync,
        ReadData
    };
    void configure();
    void onSample(double markAbs, double spaceAbs);
    void multicorrelator();
    void handleBit(int value);
    int findAlpha();
    int processBytes(int cursor);
    void processChar(int code);
    void flipSmallest(int *pos);

    double m_fs = 0.0;
    double m_centre = 1000.0;
    bool m_reverse = false;
    State m_state = SyncSetup;

    // mixers and half-bit box filters
    double m_markPh = 0.0, m_spacePh = 0.0;
    std::vector<std::complex<double>> m_markBuf, m_spaceBuf;
    std::complex<double> m_markSum {0, 0}, m_spaceSum {0, 0};
    size_t m_boxPos = 0;

    double m_markEnv = 0, m_spaceEnv = 0, m_markNoise = 0, m_spaceNoise = 0;
    double m_bitSamples = 480.0;
    long m_sampleCount = 0;
    double m_earlyAcc = 0, m_promptAcc = 0, m_lateAcc = 0;
    double m_nextEarly = 0, m_nextPrompt = 0, m_nextLate = 0;
    double m_avgEarly = 0, m_avgPrompt = 0, m_avgLate = 0;

    std::vector<int> m_bits; // one second of bit values
    int m_cursor = 0;
    bool m_alphaPhase = false;
    bool m_shift = false;
    int m_errors = 0;
    int m_lastChar = 0;
    std::string m_out;

    double m_level = 0.0, m_levelDb = -120.0;
};

class NavtexEncoder
{
public:
    void setCentreHz(double hz)
    {
        m_centre = hz;
    }
    // phasingSec of alternating phasing signals, the text, then end-of-emission alphas.
    void start(const std::string &text, double phasingSec = 3.0);
    bool done() const
    {
        return m_bit >= m_bitsOut.size();
    }
    void render(float *out, size_t n, double sampleRate);

private:
    double m_centre = 1000.0;
    std::vector<int> m_bitsOut;
    size_t m_bit = 0;
    double m_t = 0.0, m_phase = 0.0;
};
