#pragma once

// Feld Hell (Hellschreiber). Qt-free.
//
// Text is sent as a picture: each character is 7 columns (5 of glyph, 2
// blank), each column 14 half-pixels scanned bottom to top, keyed on/off on
// one carrier. 245 half-pixels a second = 17.5 columns = 2.5 characters a
// second (122.5 baud). The receiver does not decode letters: it paints the
// received envelope column by column and the operator reads it.

#include <complex>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace hell
{
constexpr int kRows = 14; // half-pixels per column
constexpr int kGlyphCols = 5;
constexpr int kCharCols = 7;
constexpr double kPixelRate = 245.0; // half-pixels per second

// 7 rows x 5 columns, row 0 at the top, '#' = on; nullptr if not in the font.
// Lower case maps to upper case.
const char *const *glyph(char c);
// The 7 columns of a character, each 14 values (bottom first) of 0/1.
// Returns false (and blanks) for characters not in the font.
bool charColumns(char c, std::vector<uint8_t> &cols);
} // namespace hell

class HellModulator
{
public:
    void setCarrier(double hz)
    {
        m_carrierHz = hz;
    }
    double carrier() const
    {
        return m_carrierHz;
    }
    void clear();
    void preroll(double sec); // blank columns
    // Returns counted columns queued so far after c, or 0 if c cannot be sent.
    size_t queueChar(char c);
    void tail(double sec); // blank columns
    bool idle() const;
    size_t unitsSent() const
    {
        return m_sent;
    }
    void render(float *out, size_t n, double sampleRate);

private:
    struct Col
    {
        uint16_t bits = 0; // bit i = half-pixel i (bottom = 0)
        bool counted = false;
    };
    std::deque<Col> m_q;
    size_t m_queued = 0;
    size_t m_sent = 0;
    double m_carrierHz = 1000.0;
    bool m_inCol = false;
    bool m_filler = false; // the current column is idle filler
    Col m_col;
    double m_px = 0.0; // position in the column, in half-pixels 0..14
    double m_prevTarget = 0.0;
    double m_curTarget = 0.0;
    int m_curPx = -1;
    double m_phase = 0.0;
};

class HellDemodulator
{
public:
    void setSampleRate(double fs);
    void setCarrier(double hz)
    {
        m_hz = hz;
    }
    // Receive speed correction in percent (slanted text: the sender's clock is off).
    void setRateTrim(double percent)
    {
        m_trim = percent;
    }
    void reset();
    // Appends complete columns (kRows values each, bottom first, 0..1 after AGC).
    void process(const float *x, size_t n, std::vector<float> &columns);
    double levelDb() const;

private:
    void rebuild();
    double m_fs = 0.0;
    double m_hz = 1000.0;
    double m_trim = 0.0;
    double m_nco = 0.0;
    std::vector<std::complex<double>> m_buf;
    std::vector<double> m_win;
    size_t m_pos = 0;
    size_t m_filled = 0;
    double m_next = 0.0;
    double m_peak = 1e-6;
    double m_floor = 0.0;
    double m_mag = 0.0;
    std::vector<float> m_col;
};
