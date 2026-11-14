#pragma once
#include <stdint.h>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_timer.h>

class MoistureTrend
{
public:
    static constexpr int MAX_ENTRIES        = 720;  // 30 days at 1-hour intervals
    static constexpr int DEFAULT_INTERVAL_S = 3600;
    static constexpr int SAVE_EVERY_N       = 4;   // persist to NVS every 4 hours

    struct Entry {
        uint32_t ts;
        int16_t  raw;
        uint16_t temp_b;  // soil temp in 0.1 C, biased by 0x8000; 0 = no reading (older entries)
    };
    static_assert(sizeof(Entry) == 8, "Entry size mismatch");

    using RawGetterFn  = std::function<int()>;
    using TempGetterFn = std::function<bool(float *temp_c)>;

    static constexpr uint16_t TEMP_NONE = 0;

    MoistureTrend();
    ~MoistureTrend();

    void load();
    void start(RawGetterFn rg, TempGetterFn tg = nullptr, int interval_s = DEFAULT_INTERVAL_S);
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
    RawGetterFn        m_rg;
    TempGetterFn       m_tg;

    void   sample();
    void   save();
    static void timer_cb(void *arg);
};
