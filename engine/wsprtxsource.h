#pragma once

// WSPR transmit audio: one 110.6 s message, started by ModemEngine (PTT,
// watchdogs) through ModemTxSource::start().

#include "modemtxsource.h"
#include "wsprcore.h"

class WsprTxSource : public ModemTxSource
{
public:
    // The message for the next start().
    void setMessage(const std::array<std::uint8_t, wspr::kSymbols> &symbols, double centreHz)
    {
        std::lock_guard<std::mutex> lock(mutex());
        m_sym = symbols;
        m_centre = centreHz;
        m_have = true;
    }
    double progress() const
    {
        std::lock_guard<std::mutex> lock(mutex());
        return m_mod.progress();
    }
    bool sending() const
    {
        std::lock_guard<std::mutex> lock(mutex());
        return m_mod.active();
    }

protected:
    void modClear() override
    {
        m_mod.stop();
    }
    void modPreroll(double) override
    {
        if (m_have)
            m_mod.start(m_sym, m_centre);
        else
            m_mod.stop();
    }
    size_t modQueueChar(char) override
    {
        return 0;
    }
    void modTail(double) override { }
    bool modIdle() const override
    {
        return !m_mod.active();
    }
    size_t modUnitsSent() const override
    {
        return 0;
    }
    void modSetCarrier(double) override { }
    void modRender(float *mono, size_t n, double fs) override
    {
        m_mod.render(mono, n, fs);
    }

private:
    wspr::Modulator m_mod;
    std::array<std::uint8_t, wspr::kSymbols> m_sym {};
    double m_centre = wspr::kCentreHz;
    bool m_have = false;
};
