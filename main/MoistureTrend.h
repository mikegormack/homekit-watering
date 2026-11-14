#pragma once
#include <stdint.h>
#include <climits>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_timer.h>

// Hourly moisture and soil temperature of each zone's soil probe, kept in NVS (30 days).
class MoistureTrend
{
public:
    static constexpr int MAX_ENTRIES        = 720;  // 30 days at 1-hour intervals
    static constexpr int DEFAULT_INTERVAL_S = 3600;
    static constexpr int SAVE_EVERY_N       = 4;   // persist to NVS every 4 hours

    static constexpr int ZONES = 2;

    struct Entry {
        uint32_t ts;
        uint8_t  moist[ZONES];  // 0.5 % steps (0..200), MOIST_NONE = no reading
        int8_t   temp[ZONES];   // 0.5 C steps (-64..63.5 C), TEMP_NONE = no reading
    };
    static_assert(sizeof(Entry) == 8, "Entry size mismatch");

    using MoistGetterFn = std::function<float(int zone)>;              // %, < 0 = no reading
    using TempGetterFn  = std::function<bool(int zone, float *temp_c)>;

    static constexpr uint8_t MOIST_NONE = 0xFF;
    static constexpr int8_t  TEMP_NONE  = INT8_MIN;

    MoistureTrend();
    ~MoistureTrend();

    void load();
    void start(MoistGetterFn mg, TempGetterFn tg = nullptr, int interval_s = DEFAULT_INTERVAL_S);
    void stop();

    int  toJson(char *buf, int len);
    void clear();

private:
    struct Store {
        uint16_t head;
        uint16_t count;
        Entry    entries[MAX_ENTRIES];
    };

    Store              m_store;
    SemaphoreHandle_t  m_mutex;
    esp_timer_handle_t m_timer      = nullptr;
    int                m_since_save = 0;
    MoistGetterFn      m_mg;
    TempGetterFn       m_tg;

    void   sample();
    void   save();
    static void timer_cb(void *arg);
};
