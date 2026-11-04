#include "modemtxsource.h"

#include <algorithm>

void ModemTxSource::setLevel(float amplitude)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_level = std::clamp(amplitude, 0.0f, 1.0f);
}

void ModemTxSource::setCarrier(double hz)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    modSetCarrier(hz);
}

void ModemTxSource::start(double prerollSec)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    modPreroll(prerollSec);
    m_echo.clear();
    m_frames.store(0, std::memory_order_relaxed);
    m_active.store(true, std::memory_order_relaxed);
}

void ModemTxSource::queueText(const std::string &text)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    modQueueText(text, m_echo);
}

void ModemTxSource::modQueueText(const std::string &text, std::deque<std::pair<size_t, char>> &echo)
{
    for (char c : text)
    {
        const size_t after = modQueueChar(c);
        if (after > 0)
            echo.emplace_back(after, c);
    }
}

void ModemTxSource::queueTail(double tailSec)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    modTail(tailSec);
}

void ModemTxSource::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active.store(false, std::memory_order_relaxed);
    modClear();
    m_echo.clear();
}

bool ModemTxSource::idle() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return modIdle();
}

std::string ModemTxSource::takeSent()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string out;
    const size_t sent = modUnitsSent();
    while (!m_echo.empty() && m_echo.front().first <= sent)
    {
        out.push_back(m_echo.front().second);
        m_echo.pop_front();
    }
    return out;
}

void ModemTxSource::render(float *out, size_t frames, int channels, float sampleRate)
{
    if (!out || frames == 0)
        return;
    channels = std::max(1, channels);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active.load(std::memory_order_relaxed) || sampleRate <= 0.0f)
    {
        std::fill(out, out + frames * size_t(channels), 0.0f);
        return;
    }
    m_buf.resize(frames);
    modRender(m_buf.data(), frames, double(sampleRate));
    for (size_t i = 0; i < frames; ++i)
    {
        const float v = m_buf[i] * m_level;
        for (int c = 0; c < channels; ++c)
            out[i * size_t(channels) + size_t(c)] = v;
    }
    m_frames.fetch_add(frames, std::memory_order_relaxed);
}

void PskTxSource::setBaud(double baud)
{
    std::lock_guard<std::mutex> lock(mutex());
    m_mod.setBaud(baud);
}

void CwTxSource::setWpm(double wpm)
{
    std::lock_guard<std::mutex> lock(mutex());
    m_mod.setWpm(wpm);
}

void CwTxSource::modQueueText(const std::string &text, std::deque<std::pair<size_t, char>> &echo)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '<')
        {
            const size_t end = text.find('>', i + 1);
            if (end != std::string::npos && end - i <= 4)
            {
                const size_t after = m_mod.queueProsign(text.substr(i + 1, end - i - 1));
                if (after > 0)
                {
                    for (size_t k = i; k <= end; ++k)
                        echo.emplace_back(after, text[k]);
                    // a prosign is a letter: the gap after it is already queued
                }
                i = end;
                continue;
            }
        }
        const size_t after = m_mod.queueChar(c);
        if (after > 0)
            echo.emplace_back(after, c);
    }
}
