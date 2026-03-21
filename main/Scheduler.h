#pragma once

#include <functional>
#include <vector>

#include <esp_timer.h>

class Scheduler
{
public:
    using Callback = std::function<void()>;

    Scheduler();
    ~Scheduler();

    // Register a scheduled event. hour/min are pointers — read at check time,
    // so UI changes to the schedule are picked up automatically.
    void addEvent(const uint8_t *hour, const uint8_t *min, Callback cb);

    // Start the periodic check timer (call after all events are registered)
    void start();

private:
    struct Event
    {
        const uint8_t *hour;
        const uint8_t *min;
        Callback        cb;
        int             last_fired_min = -1;
    };

    std::vector<Event> m_events;
    esp_timer_handle_t m_timer = nullptr;

    void check();
    static void timer_cb(void *arg);
};
