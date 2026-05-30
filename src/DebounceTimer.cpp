#include "DebounceTimer.h"

DebounceTimer::DebounceTimer(int intervalMs, QObject* parent)
    : QObject(parent)
    , m_intervalMs(intervalMs)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &DebounceTimer::onTimeout);
}

void DebounceTimer::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    cancel();
}

DebounceTimer::Mode DebounceTimer::mode() const
{
    return m_mode;
}

void DebounceTimer::setIntervalMs(int intervalMs)
{
    m_intervalMs = intervalMs;
}

int DebounceTimer::intervalMs() const
{
    return m_intervalMs;
}

void DebounceTimer::schedule()
{
    if (m_intervalMs <= 0) {
        emit triggered();
        m_dirty = false;
        return;
    }

    if (m_mode == Mode::Debounce) {
        m_timer.start(m_intervalMs);
        return;
    }

    m_dirty = true;
    if (!m_timer.isActive()) {
        emit triggered();
        m_dirty = false;
        m_timer.start(m_intervalMs);
    }
}

void DebounceTimer::cancel()
{
    m_timer.stop();
    m_dirty = false;
}

void DebounceTimer::onTimeout()
{
    if (m_mode == Mode::Debounce) {
        emit triggered();
        return;
    }

    if (m_dirty) {
        emit triggered();
        m_dirty = false;
        m_timer.start(m_intervalMs);
    }
}
