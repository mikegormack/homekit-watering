
#include "EventLog.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG      = "EventLog";
static const char *NVS_NS   = "evtlog";
static const char *NVS_KEY  = "data";

EventLog::EventLog()
    : m_mutex(xSemaphoreCreateMutex())
{
    memset(&m_store, 0, sizeof(m_store));
    // load() must be called explicitly after nvs_flash_init()
}

EventLog::~EventLog()
{
    if (m_mutex)
        vSemaphoreDelete(m_mutex);
}

void EventLog::load()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return;

    size_t sz = sizeof(m_store);
    esp_err_t err = nvs_get_blob(h, NVS_KEY, &m_store, &sz);
    nvs_close(h);

    if (err != ESP_OK || sz != sizeof(m_store))
    {
        ESP_LOGW(TAG, "No saved log or size mismatch, starting fresh");
        memset(&m_store, 0, sizeof(m_store));
    }
    else
    {
        ESP_LOGI(TAG, "Loaded %d log entries", m_store.count);
    }
}

void EventLog::save()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to open NVS for write");
        return;
    }
    esp_err_t err = nvs_set_blob(h, NVS_KEY, &m_store, sizeof(m_store));
    if (err == ESP_OK)
        nvs_commit(h);
    else
        ESP_LOGW(TAG, "nvs_set_blob failed: %d", err);
    nvs_close(h);
}

void EventLog::clear()
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    memset(&m_store, 0, sizeof(m_store));
    save();
    xSemaphoreGive(m_mutex);
    ESP_LOGI(TAG, "Event log cleared");
}

void EventLog::add(uint8_t ch, LogEvt type, uint16_t value)
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);

    int idx;
    if (m_store.count < MAX_ENTRIES)
    {
        idx = (m_store.head + m_store.count) % MAX_ENTRIES;
        m_store.count++;
    }
    else
    {
        idx          = m_store.head;
        m_store.head = (m_store.head + 1) % MAX_ENTRIES;
    }

    m_store.entries[idx] = { (uint32_t)time(nullptr), (uint8_t)type, ch, value };
    save();

    xSemaphoreGive(m_mutex);
}

int EventLog::toJson(char *buf, int len)
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);

    int n = 0;
    n += snprintf(buf + n, len - n, "[");

    for (int i = 0; i < m_store.count && n < len - 4; i++)
    {
        int          idx = (m_store.head + m_store.count - 1 - i) % MAX_ENTRIES;
        const Entry &e   = m_store.entries[idx];
        n += snprintf(buf + n, len - n,
            "%s{\"ts\":%lu,\"ch\":%d,\"type\":%d,\"val\":%d}",
            i ? "," : "",
            (unsigned long)e.ts, (int)e.ch, (int)e.type, (int)e.value);
    }

    n += snprintf(buf + n, len - n, "]");

    xSemaphoreGive(m_mutex);
    return n;
}
