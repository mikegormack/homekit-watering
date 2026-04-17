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

    struct SchedTime { uint8_t hour; uint8_t min; };
    using TimeGetter = std::function<SchedTime()>;

    // Register a scheduled event. get_time is called at check time,
    // so UI changes to the schedule are picked up automatically.
    void addEvent(TimeGetter get_time, Callback cb);

    // Start the periodic check timer (call after all events are registered)
    void start();

private:
    struct Event
    {
        TimeGetter get_time;
        Callback   cb;
        int        last_fired_min = -1;
        int        last_fired_day = -1;
    };

    std::vector<Event> m_events;
    esp_timer_handle_t m_timer = nullptr;

    void check();
    static void timer_cb(void *arg);
};
