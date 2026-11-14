
#include "MoistureTrend.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG     = "MoistureTrend";
static const char *NVS_NS  = "mtrend";
static const char *NVS_KEY = "data";

MoistureTrend::MoistureTrend()
    : m_mutex(xSemaphoreCreateMutex())
{
    memset(&m_store, 0, sizeof(m_store));
}

MoistureTrend::~MoistureTrend()
{
    stop();
    if (m_mutex)
        vSemaphoreDelete(m_mutex);
}

void MoistureTrend::load()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return;

    size_t    sz  = sizeof(m_store);
    esp_err_t err = nvs_get_blob(h, NVS_KEY, &m_store, &sz);
    nvs_close(h);

    if (err != ESP_OK || sz != sizeof(m_store))
    {
        ESP_LOGW(TAG, "No saved trend data or size mismatch, starting fresh");
        memset(&m_store, 0, sizeof(m_store));
    }
    else
    {
        ESP_LOGI(TAG, "Loaded %d trend entries", (int)m_store.count);
    }
}

void MoistureTrend::save()
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

void MoistureTrend::sample()
{
    if (!m_rg)
        return;

    // Skip until SNTP has synced (year >= 2020)
    uint32_t ts = (uint32_t)time(nullptr);
    if (ts < 1577836800u)
        return;

    int raw = m_rg();

    uint16_t temp_b = TEMP_NONE;
    float    temp_c = 0;
    if (m_tg && m_tg(&temp_c))
    {
        int t10 = (int)(temp_c * 10.0f + (temp_c >= 0 ? 0.5f : -0.5f));
        temp_b  = (uint16_t)(t10 + 0x8000);
    }

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

    m_store.entries[idx] = {ts, (int16_t)raw, temp_b};

    if (++m_since_save >= SAVE_EVERY_N)
    {
        m_since_save = 0;
        save();
    }

    xSemaphoreGive(m_mutex);
    if (temp_b != TEMP_NONE)
        ESP_LOGI(TAG, "Trend sample: raw=%d temp=%.1fC", raw, temp_c);
    else
        ESP_LOGI(TAG, "Trend sample: raw=%d", raw);
}

void MoistureTrend::timer_cb(void *arg)
{
    static_cast<MoistureTrend *>(arg)->sample();
}

void MoistureTrend::start(RawGetterFn rg, TempGetterFn tg, int interval_s)
{
    m_rg = rg;
    m_tg = tg;

    esp_timer_create_args_t args = {
        .callback              = timer_cb,
        .arg                   = this,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "mtrend",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &m_timer);
    esp_timer_start_periodic(m_timer, (uint64_t)interval_s * 1000000ULL);
    ESP_LOGI(TAG, "Trend sampling started, interval=%ds", interval_s);
}

void MoistureTrend::stop()
{
    if (m_timer)
    {
        esp_timer_stop(m_timer);
        esp_timer_delete(m_timer);
        m_timer = nullptr;
    }
}

void MoistureTrend::clear()
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    memset(&m_store, 0, sizeof(m_store));
    m_since_save = 0;
    save();
    xSemaphoreGive(m_mutex);
    ESP_LOGI(TAG, "Trend log cleared");
}

int MoistureTrend::toJson(char *buf, int len)
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);

    int n = 0;
    buf[n++] = '[';

    // Oldest-first (chronological) for charting
    for (int i = 0; i < m_store.count; i++)
    {
        int          idx = (m_store.head + i) % MAX_ENTRIES;
        const Entry &e   = m_store.entries[idx];
        // "t" = soil temp in 0.1 C, left out when there's no reading
        int w;
        if (e.temp_b != TEMP_NONE)
            w = snprintf(buf + n, len - n - 1, "%s{\"ts\":%lu,\"r\":%d,\"t\":%d}", i ? "," : "",
                         (unsigned long)e.ts, (int)e.raw, (int)e.temp_b - 0x8000);
        else
            w = snprintf(buf + n, len - n - 1, "%s{\"ts\":%lu,\"r\":%d}", i ? "," : "",
                         (unsigned long)e.ts, (int)e.raw);
        if (w < 0 || w >= len - n - 1) break;
        n += w;
    }

    buf[n++] = ']';
    buf[n]   = '\0';

    xSemaphoreGive(m_mutex);
    return n;
}
