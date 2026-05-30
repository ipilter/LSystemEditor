#pragma once

#include <QObject>
#include <QTimer>

class DebounceTimer : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Debounce,
        Throttle,
    };

    explicit DebounceTimer(int intervalMs, QObject* parent = nullptr);

    void setMode(Mode mode);
    Mode mode() const;

    void setIntervalMs(int intervalMs);
    int intervalMs() const;

    void schedule();
    void cancel();

signals:
    void triggered();

private:
    void onTimeout();

    QTimer m_timer;
    int m_intervalMs = 0;
    Mode m_mode = Mode::Debounce;
    bool m_dirty = false;
};
