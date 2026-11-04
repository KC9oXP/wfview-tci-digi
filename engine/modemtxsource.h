#pragma once

// Thread-safe transmit audio source shared by the PSK and Feld Hell modes
// (RTTY keeps its own RttyTxSource).
//
//   engine (GUI thread): start(), queueText(), queueTail(), stop(), takeSent()
//   TCI client TX callback (any thread): render()
//
// A subclass supplies the modulator; this class adds the mutex, the TX level,
// the "sent so far" echo and the frame counter used by the TX watchdog.

#include "hellcore.h"
#include "pskcore.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class ModemTxSource
{
public:
    virtual ~ModemTxSource() = default;

    void setLevel(float amplitude); // 0..1 of full scale
    void setCarrier(double hz);

    void start(double prerollSec); // clear, preroll, output on
    void queueText(const std::string &text); // characters the mode cannot send are dropped
    void queueTail(double tailSec);
    void stop(); // output off now, everything dropped

    bool active() const
    {
        return m_active.load(std::memory_order_relaxed);
    }
    bool idle() const;
    std::string takeSent(); // characters fully sent since the last call
    uint64_t renderedFrames() const
    {
        return m_frames.load(std::memory_order_relaxed);
    }
    void render(float *out, size_t frames, int channels, float sampleRate);

protected:
    std::mutex &mutex() const
    {
        return m_mutex;
    }
    // All called with the mutex held.
    virtual void modClear() = 0;
    virtual void modPreroll(double sec) = 0;
    virtual size_t modQueueChar(char c) = 0; // counted units after c, 0 = dropped
    virtual void modTail(double sec) = 0;
    virtual bool modIdle() const = 0;
    virtual size_t modUnitsSent() const = 0;
    virtual void modSetCarrier(double hz) = 0;
    virtual void modRender(float *mono, size_t n, double sampleRate) = 0;

private:
    mutable std::mutex m_mutex;
    float m_level = 0.5f;
    std::deque<std::pair<size_t, char>> m_echo;
    std::vector<float> m_buf;
    std::atomic<bool> m_active {false};
    std::atomic<uint64_t> m_frames {0};
};

class PskTxSource : public ModemTxSource
{
public:
    void setBaud(double baud);

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
    PskModulator m_mod;
};

class HellTxSource : public ModemTxSource
{
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
    HellModulator m_mod;
};
