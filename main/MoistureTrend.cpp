
#include "MoistureTrend.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG     = "MoistureTrend";
static const char *NVS_NS  = "mtrend";
static const char *NVS_KEY     = "zones";  // per-zone probe readings
static const char *NVS_KEY_OLD = "data";   // raw readings of the old single soil sensor, no longer used

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

    // Free the space of the old single-sensor log (raw readings can't be turned into probe %)
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK)
    {
        if (nvs_erase_key(h, NVS_KEY_OLD) == ESP_OK)
        {
            nvs_commit(h);
            ESP_LOGI(TAG, "Removed old single-sensor trend data");
        }
        nvs_close(h);
    }

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
    if (!m_mg)
        return;

    // Skip until SNTP has synced (year >= 2020)
    uint32_t ts = (uint32_t)time(nullptr);
    if (ts < 1577836800u)
        return;

    Entry e   = {};
    e.ts      = ts;
    bool any  = false;
    for (int z = 0; z < ZONES; z++)
    {
        float m     = m_mg(z);
        e.moist[z]  = MOIST_NONE;
        if (m >= 0)
        {
            int v      = (int)(m * 2.0f + 0.5f);
            e.moist[z] = (uint8_t)(v > 200 ? 200 : v);
            any        = true;
        }
        float t    = 0;
        e.temp[z]  = TEMP_NONE;
        if (m_tg && m_tg(z, &t))
        {
            int v     = (int)(t * 2.0f + (t >= 0 ? 0.5f : -0.5f));
            e.temp[z] = (int8_t)(v < -127 ? -127 : v > 127 ? 127 : v);
            any       = true;
        }
    }
    if (!any)
        return;  // no zone has a probe reading: nothing worth storing

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

    m_store.entries[idx] = e;

    if (++m_since_save >= SAVE_EVERY_N)
    {
        m_since_save = 0;
        save();
    }

    xSemaphoreGive(m_mutex);
    ESP_LOGI(TAG, "Trend sample: CH1 %d CH2 %d (0.5 %% steps, 255 = none)", e.moist[0], e.moist[1]);
}

void MoistureTrend::timer_cb(void *arg)
{
    static_cast<MoistureTrend *>(arg)->sample();
}

void MoistureTrend::start(MoistGetterFn mg, TempGetterFn tg, int interval_s)
{
    m_mg = mg;
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

    // Oldest-first (chronological) for charting, compact rows to keep the page's buffer small:
    // [ts, CH1 %, CH2 %, CH1 C, CH2 C], null where there was no reading
    for (int i = 0; i < m_store.count; i++)
    {
        int          idx = (m_store.head + i) % MAX_ENTRIES;
        const Entry &e   = m_store.entries[idx];
        char         v[ZONES * 2][8];
        for (int z = 0; z < ZONES; z++)
        {
            if (e.moist[z] == MOIST_NONE)
                snprintf(v[z], sizeof(v[z]), "null");
            else
                snprintf(v[z], sizeof(v[z]), "%d.%d", e.moist[z] / 2, (e.moist[z] & 1) * 5);
            if (e.temp[z] == TEMP_NONE)
                snprintf(v[ZONES + z], sizeof(v[z]), "null");
            else
                snprintf(v[ZONES + z], sizeof(v[z]), "%.1f", e.temp[z] / 2.0f);
        }
        int w = snprintf(buf + n, len - n - 1, "%s[%lu,%s,%s,%s,%s]", i ? "," : "", (unsigned long)e.ts, v[0], v[1],
                         v[2], v[3]);
        if (w < 0 || w >= len - n - 1) break;
        n += w;
    }

    buf[n++] = ']';
    buf[n]   = '\0';

    xSemaphoreGive(m_mutex);
    return n;
}
