#pragma once

// Transmit sources for Olivia / Contestia, MT63, AMTOR and SSTV, on the shared
// ModemTxSource (mutex, level, echo, frame counter). Kept apart from
// modemtxsource.h so the PSK / Hell / CW tests do not need these modems.

#include "modemtxsource.h"
#include "mt63core.h"
#include "navtexcore.h"
#include "oliviacore.h"
#include "sstvcore.h"

#include <cstdint>
#include <vector>

class OliviaTxSource : public ModemTxSource
{
public:
    void setConfig(const olivia::Config &c)
    {
        std::lock_guard<std::mutex> lock(mutex());
        m_mod.setConfig(c);
    }

protected:
    void modClear() override
    {
        m_mod.clear();
    }
    void modPreroll(double sec) override
    {
        m_mod.preroll(sec);
    }
    size_t modQueueChar(char c) override
    {
        return m_mod.queueChar(c);
    }
    void modTail(double sec) override
    {
        m_mod.tail(sec);
    }
    bool modIdle() const override
    {
        return m_mod.idle();
    }
    size_t modUnitsSent() const override
    {
        return m_mod.unitsSent();
    }
    void modSetCarrier(double hz) override
    {
        m_mod.setCarrier(hz);
    }
    void modRender(float *mono, size_t n, double fs) override
    {
        m_mod.render(mono, n, fs);
    }

private:
    OliviaModulator m_mod;
};

class Mt63TxSource : public ModemTxSource
{
public:
    void setConfig(const mt63::Config &c)
    {
        std::lock_guard<std::mutex> lock(mutex());
        m_mod.setConfig(c);
    }

protected:
    void modClear() override
    {
        m_mod.clear();
    }
    void modPreroll(double sec) override
    {
        m_mod.preroll(sec);
    }
    size_t modQueueChar(char c) override
    {
        return m_mod.queueChar(c);
    }
    void modTail(double sec) override
    {
        m_mod.tail(sec);
    }
    bool modIdle() const override
    {
        return m_mod.idle();
    }
    size_t modUnitsSent() const override
    {
        return m_mod.unitsSent();
    }
    void modSetCarrier(double hz) override
    {
        m_mod.setCarrier(hz);
    }
    void modRender(float *mono, size_t n, double fs) override
    {
        m_mod.render(mono, n, fs);
    }

private:
    Mt63Modulator m_mod;
};

// AMTOR FEC (mode B): typed text, phasing first, idle beta between words.
class AmtorTxSource : public ModemTxSource
{
public:
    void setReverse(bool on)
    {
        std::lock_guard<std::mutex> lock(mutex());
        m_mod.setReverse(on);
    }

protected:
    void modClear() override
    {
        m_mod.clear();
    }
    void modPreroll(double sec) override
    {
        m_mod.preroll(sec);
    }
    size_t modQueueChar(char c) override
    {
        return m_mod.queueChar(c);
    }
    void modTail(double sec) override
    {
        m_mod.tail(sec);
    }
    bool modIdle() const override
    {
        return m_mod.idle();
    }
    size_t modUnitsSent() const override
    {
        return m_mod.unitsSent();
    }
    void modSetCarrier(double hz) override
    {
        m_mod.setCentreHz(hz);
    }
    void modRender(float *mono, size_t n, double fs) override
    {
        m_mod.render(mono, n, fs);
    }

private:
    AmtorFecModulator m_mod;
};

// SSTV: setImage() before keying; start() sends the VIS header and the
// picture; idle once it is all out.
class SstvTxSource : public ModemTxSource
{
public:
    void setImage(int mode, const std::vector<uint32_t> &rgb)
    {
        std::lock_guard<std::mutex> lock(mutex());
        m_mode = mode;
        m_rgb = rgb;
    }
    // Sound card clock correction for the next picture (0 = none).
    void setTimingPpm(double ppm)
    {
        std::lock_guard<std::mutex> lock(mutex());
        m_timingPpm = ppm;
    }
    double progress() const
    {
        std::lock_guard<std::mutex> lock(mutex());
        return m_enc.progress();
    }
    double totalSeconds() const
    {
        std::lock_guard<std::mutex> lock(mutex());
        return m_enc.totalSeconds();
    }
    // Image line on the air (-1 in the VIS header) and the picture height.
    int lineSending() const
    {
        std::lock_guard<std::mutex> lock(mutex());
        return m_enc.lineSending();
    }
    int imageLines() const
    {
        std::lock_guard<std::mutex> lock(mutex());
        const int m = m_enc.mode();
        return m >= 0 ? sstv::info(m).height : 0;
    }

protected:
    void modClear() override
    {
        m_enc.clear();
    }
    void modPreroll(double) override
    {
        m_enc.setTimingPpm(m_timingPpm);
        if (m_mode >= 0 && !m_rgb.empty())
            m_enc.start(m_mode, m_rgb);
        else
            m_enc.clear();
    }
    size_t modQueueChar(char) override
    {
        return 0;
    }
    void modTail(double) override { }
    bool modIdle() const override
    {
        return !m_enc.active();
    }
    size_t modUnitsSent() const override
    {
        return 0;
    }
    void modSetCarrier(double) override { }
    void modRender(float *mono, size_t n, double fs) override
    {
        m_enc.render(mono, n, fs);
    }

private:
    SstvEncoder m_enc;
    int m_mode = -1;
    std::vector<uint32_t> m_rgb;
    double m_timingPpm = 0.0;
};
