#include "rttytxsource.h"

#include <algorithm>

void RttyTxSource::setConfig(const RttyConfig &cfg)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const double rate = m_cfg.sampleRate;
    m_cfg = cfg;
    m_cfg.sampleRate = rate; // keep the rate render() last used
    m_mod.setConfig(m_cfg);
}

void RttyTxSource::setLevel(float amplitude)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mod.setAmplitude(std::clamp(amplitude, 0.0f, 1.0f));
}

void RttyTxSource::setDiddle(bool on)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mod.setDiddle(on);
}

void RttyTxSource::start(double prerollSec)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mod.clear();
    m_echo.clear();
    m_figs = false;
    m_mod.queueMark(prerollSec);
    m_mod.queueCodes({RttyBaudot::kLtrs});
    m_queuedCodes = 1;
    m_frames.store(0, std::memory_order_relaxed);
    m_monitor.clear();
    m_active.store(true, std::memory_order_relaxed);
}

void RttyTxSource::queueText(const std::string &text)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (char ch : text)
    {
        const std::vector<uint8_t> codes = RttyBaudot::encode(std::string(1, ch), m_figs, m_cfg.usos, m_cfg.ita2);
        if (codes.empty())
            continue;
        m_mod.queueCodes(codes);
        m_queuedCodes += codes.size();
        m_echo.emplace_back(m_queuedCodes, ch == '\r' ? '\n' : ch);
    }
}

void RttyTxSource::queueTail(double tailSec)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mod.queueMark(tailSec);
}

void RttyTxSource::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_active.store(false, std::memory_order_relaxed);
    m_mod.clear();
    m_echo.clear();
    m_queuedCodes = 0;
    m_monitor.clear();
}

bool RttyTxSource::idle() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mod.idle();
}

std::string RttyTxSource::takeSent()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string out;
    const size_t sent = m_mod.codesSent();
    while (!m_echo.empty() && m_echo.front().first <= sent)
    {
        out.push_back(m_echo.front().second);
        m_echo.pop_front();
    }
    return out;
}

void RttyTxSource::render(float *out, size_t frames, int channels, float sampleRate)
{
    if (!out || frames == 0)
        return;
    channels = std::max(1, channels);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (sampleRate > 0.0f && m_cfg.sampleRate != sampleRate)
    {
        m_cfg.sampleRate = sampleRate;
        m_mod.setConfig(m_cfg);
    }
    m_buf.resize(frames);
    m_mod.render(m_buf.data(), frames);
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            out[i * channels + c] = m_buf[i];
    m_frames.fetch_add(frames, std::memory_order_relaxed);

    if (m_active.load(std::memory_order_relaxed) && m_cfg.sampleRate > 0.0)
    {
        m_monitorRate = static_cast<float>(m_cfg.sampleRate);
        m_monitor.insert(m_monitor.end(), m_buf.begin(), m_buf.end());
        const size_t cap = static_cast<size_t>(m_cfg.sampleRate); // about one second
        if (m_monitor.size() > cap)
            m_monitor.erase(m_monitor.begin(), m_monitor.end() - static_cast<std::ptrdiff_t>(cap));
    }
}

float RttyTxSource::takeMonitor(std::vector<float> &out)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    out.clear();
    if (m_monitor.empty())
        return 0.0f;
    out.swap(m_monitor);
    return m_monitorRate;
}
