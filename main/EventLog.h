#pragma once

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class LogEvt : uint8_t {
    MANUAL_START   = 0x01,  // value = duration_s
    MANUAL_STOP    = 0x02,
    SCHED_START    = 0x03,  // value = duration_s
    SCHED_SKIP_DUR = 0x04,
    SCHED_SKIP_MOI = 0x05,  // value = moisture %
    HAP_START      = 0x06,  // value = duration_s
    HAP_STOP       = 0x07,
    RUN_COMPLETE   = 0x08,
    SCHED_DISABLED = 0x09,
    BOOT           = 0x0A,  // ch = 0xFF (system); value = esp_reset_reason_t
    CRASH          = 0x0B,  // ch = 0xFF (system); value = esp_reset_reason_t
    OTA_COMPLETE   = 0x0C,  // ch = 0xFF (system)
};

class EventLog
{
public:
    static constexpr int MAX_ENTRIES = 256;

    struct Entry {
        uint32_t ts;    // unix timestamp
        uint8_t  type;  // LogEvt cast
        uint8_t  ch;    // channel index (0-based)
        uint16_t value;
    };
    static_assert(sizeof(Entry) == 8, "Entry must be 8 bytes");

    EventLog();
    ~EventLog();

    void load();  // call once after nvs_flash_init(); safe to call again
    void add(uint8_t ch, LogEvt type, uint16_t value = 0);
    void clear();

    int  toJson(char *buf, int len);       // serialize all entries as JSON array; returns bytes written

private:
    struct Store {
        uint8_t head;
        uint8_t count;
        uint8_t _pad[2];
        Entry   entries[MAX_ENTRIES];
    };

    Store             m_store;
    SemaphoreHandle_t m_mutex;

    void save();
};
