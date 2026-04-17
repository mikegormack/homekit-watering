
#include "Scheduler.h"

#include <ctime>
#include <esp_log.h>

static const char *TAG = "Scheduler";

Scheduler::Scheduler()
{
}

Scheduler::~Scheduler()
{
    if (m_timer)
    {
        esp_timer_stop(m_timer);
        esp_timer_delete(m_timer);
    }
}

void Scheduler::addEvent(TimeGetter get_time, Callback cb)
{
    m_events.push_back({std::move(get_time), std::move(cb), -1});
}

void Scheduler::start()
{
    esp_timer_create_args_t args = {
        .callback              = timer_cb,
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "scheduler",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &m_timer);
    esp_timer_start_periodic(m_timer, 10 * 1000000ULL); // check every 10 seconds
    ESP_LOGI(TAG, "Started (%d events)", (int)m_events.size());
}

void Scheduler::timer_cb(void *arg)
{
    static_cast<Scheduler *>(arg)->check();
}

void Scheduler::check()
{
    std::time_t now;
    std::time(&now);
    std::tm local_tm;
    std::tm *local = localtime_r(&now, &local_tm);

    // Don't fire until NTP has set the clock (year 1970 = not yet synced)
    if (local->tm_year < 120) // tm_year: years since 1900; 120 = 2020
        return;

    // Only act in the first 30 seconds of a minute to avoid stale firing
    if (local->tm_sec >= 30)
        return;

    for (auto &ev : m_events)
    {
        SchedTime t = ev.get_time();
        if (t.hour != (uint8_t)local->tm_hour || t.min != (uint8_t)local->tm_min)
            continue;
        if (ev.last_fired_day == local->tm_yday && ev.last_fired_min == local->tm_min)
            continue; // already fired this minute today

        ev.last_fired_day = local->tm_yday;
        ev.last_fired_min = local->tm_min;
        ESP_LOGI(TAG, "Firing event at %02d:%02d", t.hour, t.min);
        ev.cb();
    }
}
