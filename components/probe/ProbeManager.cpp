#include "ProbeManager.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <nvs.h>

static const char *TAG = "Probes";

using namespace probe_reg;

static constexpr int MB_ILLEGAL_ADDRESS = 2;  // Modbus exception code

static constexpr const char *NVS_NS  = "storage";
static constexpr const char *NVS_KEY = "probes";

namespace
{
struct StoredProbe
{
    uint8_t addr;
    char    name[ProbeManager::NAME_LEN];
};

struct Lock
{
    SemaphoreHandle_t m;
    explicit Lock(SemaphoreHandle_t m) : m(m) { xSemaphoreTakeRecursive(m, portMAX_DELAY); }
    ~Lock() { xSemaphoreGiveRecursive(m); }
};

int64_t now_us() { return esp_timer_get_time(); }

bool valid_addr(int a) { return a >= 1 && a <= 247; }
}  // namespace

ProbeManager::ProbeManager() : m_lock(xSemaphoreCreateRecursiveMutex()) {}

// ---------------------------------------------------------------------------
// Probe list
// ---------------------------------------------------------------------------

void ProbeManager::load()
{
    Lock l(m_lock);
    m_probes.clear();

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return;
    StoredProbe sp[MAX_PROBES];
    size_t      len = sizeof(sp);
    if (nvs_get_blob(h, NVS_KEY, sp, &len) == ESP_OK)
    {
        for (size_t i = 0; i < len / sizeof(StoredProbe); i++)
        {
            if (!valid_addr(sp[i].addr))
                continue;
            Probe p{};
            p.addr = sp[i].addr;
            memcpy(p.name, sp[i].name, NAME_LEN);
            p.name[NAME_LEN - 1] = '\0';
            m_probes.push_back(p);
        }
    }
    nvs_close(h);
    ESP_LOGI(TAG, "%d probe(s) configured", (int)m_probes.size());
}

void ProbeManager::save()
{
    Lock        l(m_lock);
    StoredProbe sp[MAX_PROBES] = {};
    size_t      n              = std::min((size_t)MAX_PROBES, m_probes.size());
    for (size_t i = 0; i < n; i++)
    {
        sp[i].addr = m_probes[i].addr;
        memcpy(sp[i].name, m_probes[i].name, NAME_LEN);
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK)
        return;
    if (n)
        nvs_set_blob(h, NVS_KEY, sp, n * sizeof(StoredProbe));
    else
        nvs_erase_key(h, NVS_KEY);
    nvs_commit(h);
    nvs_close(h);
}

std::vector<ProbeManager::Probe> ProbeManager::list()
{
    Lock l(m_lock);
    return m_probes;
}

bool ProbeManager::get(uint8_t addr, Probe &out)
{
    Lock l(m_lock);
    for (auto &p : m_probes)
        if (p.addr == addr)
        {
            out = p;
            return true;
        }
    return false;
}

float ProbeManager::moisture(uint8_t addr)
{
    Probe p;
    if (!addr || !get(addr, p))
        return -1.0f;
    const Reading &r     = p.r;
    int64_t        stale = std::max<int64_t>(3LL * m_poll_ms * 1000, 60LL * 1000000);
    if (!r.valid || !r.online || r.in_bootloader || (r.status & ST_UNCALIBRATED) || now_us() - r.last_ok_us > stale)
        return -1.0f;
    return r.moisture / 100.0f;
}

bool ProbeManager::temperature(uint8_t addr, float *temp_c)
{
    Probe p;
    if (!addr || !temp_c || !get(addr, p))
        return false;
    const Reading &r     = p.r;
    int64_t        stale = std::max<int64_t>(3LL * m_poll_ms * 1000, 60LL * 1000000);
    if (!r.valid || !r.online || r.in_bootloader || (r.status & ST_NTC_FAULT) || now_us() - r.last_ok_us > stale)
        return false;
    *temp_c = r.temp / 100.0f;
    return true;
}

int ProbeManager::add(uint8_t addr, const char *name)
{
    if (!valid_addr(addr))
        return ModbusMaster::ERR_ARG;
    {
        Lock l(m_lock);
        for (auto &p : m_probes)
            if (p.addr == addr)
                return ERR_EXISTS;
        if ((int)m_probes.size() >= MAX_PROBES)
            return ERR_FULL;
        Probe p{};
        p.addr = addr;
        snprintf(p.name, NAME_LEN, "%s", (name && *name) ? name : "Probe");
        m_probes.push_back(p);
        save();
    }
    pollNow();
    ESP_LOGI(TAG, "Added probe %d", addr);
    return 0;
}

int ProbeManager::remove(uint8_t addr)
{
    Lock l(m_lock);
    auto it = std::find_if(m_probes.begin(), m_probes.end(), [addr](const Probe &p) { return p.addr == addr; });
    if (it == m_probes.end())
        return ERR_NOT_FOUND;
    m_probes.erase(it);
    save();
    ESP_LOGI(TAG, "Removed probe %d", addr);
    return 0;
}

int ProbeManager::rename(uint8_t addr, const char *name)
{
    Lock l(m_lock);
    for (auto &p : m_probes)
        if (p.addr == addr)
        {
            snprintf(p.name, NAME_LEN, "%s", (name && *name) ? name : "Probe");
            save();
            return 0;
        }
    return ERR_NOT_FOUND;
}

void ProbeManager::pollNow()
{
    m_poll_now = true;
    if (m_task)
        xTaskNotifyGive(m_task);
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

bool ProbeManager::begin(const ModbusMaster::Config &bus, uint32_t poll_interval_s)
{
    m_poll_ms = std::max<uint32_t>(poll_interval_s, 1) * 1000;
    load();
    if (!m_bus.begin(bus))
        return false;
    xTaskCreate(task_entry, "probes", 4096, this, 2, &m_task);
    return true;
}

int ProbeManager::pollOne(uint8_t addr, Reading &r)
{
    uint16_t v[IN_NUM];
    int      e = m_bus.readInput(addr, 0, IN_NUM, v, 300);
    if (e == MB_ILLEGAL_ADDRESS)
    {
        // The bootloader only has input registers 8 (update status) and 9 (version 0xB0xx).
        uint16_t b[2];
        if (m_bus.readInput(addr, IN_STATUS, 2, b, 300) == 0 && is_boot_version(b[1]))
        {
            r.in_bootloader = true;
            r.bl_status     = b[0];
            r.bl_version    = b[1];
            return 0;  // no measurements, keep the last ones
        }
    }
    if (e)
        return e;

    r.in_bootloader = false;
    r.status        = v[IN_STATUS];
    r.fw_version    = v[IN_VERSION];
    r.sense         = v[IN_SENSE];
    r.ref           = v[IN_REF];
    r.ntc           = v[IN_NTC];
    r.vrefint       = v[IN_VREFINT];
    r.ratio         = v[IN_RATIO];
    r.moisture      = (int16_t)v[IN_MOISTURE];
    r.temp          = (int16_t)v[IN_TEMP];
    r.vdda_mv       = v[IN_VDDA];
    r.count         = v[IN_COUNT];
    r.moisture_raw  = (int16_t)v[IN_MOISTURE_RAW];
    return 0;
}

void ProbeManager::pollAll()
{
    std::vector<Probe> snap = list();
    for (auto &p : snap)
    {
        bool was_online = p.r.online;
        int  e          = pollOne(p.addr, p.r);
        if (e == 0)
        {
            p.r.valid      = true;
            p.r.online     = true;
            p.r.fails      = 0;
            p.r.last_ok_us = now_us();
        }
        else
        {
            if (p.r.fails < 255)
                p.r.fails++;
            if (p.r.fails >= OFFLINE_AFTER)
                p.r.online = false;
            ESP_LOGD(TAG, "probe %d: %s", p.addr, ModbusMaster::errStr(e));
        }

        {
            Lock l(m_lock);
            for (auto &q : m_probes)
                if (q.addr == p.addr)
                    q.r = p.r;
        }

        if (on_event && was_online != p.r.online && (p.r.online || p.r.valid))
            on_event(p.r.online ? Event::ONLINE : Event::OFFLINE, p.addr, 0);
        if (on_reading)
            on_reading(p);
    }
}

void ProbeManager::task_entry(void *arg)
{
    ProbeManager *self      = static_cast<ProbeManager *>(arg);
    int64_t       next_poll = 0;
    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));

        JobType req;
        uint8_t addr;
        {
            Lock l(self->m_lock);
            req             = self->m_job_req;
            addr            = self->m_job.addr;
            self->m_job_req = JobType::NONE;
        }
        if (req == JobType::SCAN)
            self->runScan();
        else if (req == JobType::UPDATE)
            self->runUpdate(addr);
        if (req != JobType::NONE)
        {
            self->m_job_running = false;
            next_poll           = 0;
        }

        if (self->m_poll_now.exchange(false) || now_us() >= next_poll)
        {
            self->pollAll();
            next_poll = now_us() + (int64_t)self->m_poll_ms * 1000;
        }
    }
}

// ---------------------------------------------------------------------------
// Direct operations
// ---------------------------------------------------------------------------

int ProbeManager::readSettings(uint8_t addr, Settings &s)
{
    if (m_job_running)
        return ERR_BUSY;
    uint16_t v[11];
    int      e = m_bus.readHolding(addr, 0, 11, v, 300);
    if (e)
        return e;
    s.slave_addr  = v[HR_ADDR];
    s.baud_100    = v[HR_BAUD];
    s.parity      = v[HR_PARITY];
    s.cal_dry     = v[HR_CAL_DRY];
    s.cal_wet     = v[HR_CAL_WET];
    s.tc_coef     = (int16_t)v[HR_TC_COEF];
    s.tc_ref      = (int16_t)v[HR_TC_REF];
    s.flags       = v[HR_FLAGS];
    s.mco_div     = v[HR_MCO_DIV];
    s.settle_ms   = v[HR_SETTLE];
    s.interval_ms = v[HR_INTERVAL];
    return 0;
}

int ProbeManager::writeCalibration(uint8_t addr, const Settings &s, bool save_flash)
{
    if (m_job_running)
        return ERR_BUSY;
    uint16_t v[8] = {s.cal_dry,   s.cal_wet,  (uint16_t)s.tc_coef, (uint16_t)s.tc_ref,
                     s.flags,     s.mco_div,  s.settle_ms,         s.interval_ms};
    int      e    = m_bus.writeMultiple(addr, HR_CAL_DRY, 8, v, 300);
    if (e == 0 && save_flash)
        e = m_bus.writeSingle(addr, HR_CMD, CMD_SAVE, 500);
    pollNow();
    return e;
}

int ProbeManager::command(uint8_t addr, uint16_t cmd)
{
    if (m_job_running)
        return ERR_BUSY;
    int e = m_bus.writeSingle(addr, HR_CMD, cmd, 500);
    pollNow();
    return e;
}

int ProbeManager::changeAddress(uint8_t old_addr, uint8_t new_addr)
{
    if (m_job_running)
        return ERR_BUSY;
    if (!valid_addr(old_addr) || !valid_addr(new_addr) || old_addr == new_addr)
        return ModbusMaster::ERR_ARG;
    {
        Lock l(m_lock);
        for (auto &p : m_probes)
            if (p.addr == new_addr)
                return ERR_EXISTS;
    }
    uint16_t st;
    if (m_bus.readInput(new_addr, IN_STATUS, 1, &st, 150) != ModbusMaster::ERR_TIMEOUT)
        return ERR_EXISTS;  // something already answers on the new address

    int e = m_bus.writeSingle(old_addr, HR_ADDR, new_addr, 300);
    if (e == 0)
        e = m_bus.writeSingle(old_addr, HR_CMD, CMD_SAVE, 500);
    if (e == 0)
        e = m_bus.writeSingle(old_addr, HR_CMD, CMD_REBOOT, 300);
    if (e)
        return e;

    {
        Lock l(m_lock);
        for (auto &p : m_probes)
            if (p.addr == old_addr)
            {
                p.addr = new_addr;
                p.r    = Reading{};
            }
        save();
    }
    if (on_readdress)
        on_readdress(old_addr, new_addr);
    ESP_LOGI(TAG, "Probe %d moved to address %d", old_addr, new_addr);

    // The probe restarts through its bootloader (~0.3 s) before it answers on the new address.
    vTaskDelay(pdMS_TO_TICKS(800));
    for (int i = 0; i < 10; i++)
    {
        if (m_bus.readInput(new_addr, IN_STATUS, 1, &st, 200) == 0)
        {
            pollNow();
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return ERR_VERIFY;
}

// ---------------------------------------------------------------------------
// Firmware image
// ---------------------------------------------------------------------------

uint32_t ProbeManager::crc32(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    while (len--)
    {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

int ProbeManager::imageBegin(size_t size)
{
    if (m_job_running)
        return ERR_BUSY;
    if (size < 8 || size > APP_SIZE_MAX)
        return ERR_IMAGE;
    m_img_ready = false;
    m_img_size  = 0;
    m_img_part  = esp_ota_get_next_update_partition(nullptr);
    if (!m_img_part || m_img_part->size < APP_SIZE_MAX)
        return ERR_IMAGE;
    uint32_t erase = (APP_SIZE_MAX + 4095) & ~4095u;
    if (esp_partition_erase_range(m_img_part, 0, erase) != ESP_OK)
        return ERR_IMAGE;
    return 0;
}

int ProbeManager::imageWrite(const void *data, size_t len)
{
    if (!m_img_part || m_img_ready || m_img_size + len > APP_SIZE_MAX)
        return ERR_IMAGE;
    if (esp_partition_write(m_img_part, m_img_size, data, len) != ESP_OK)
        return ERR_IMAGE;
    m_img_size += len;
    return 0;
}

int ProbeManager::imageEnd()
{
    if (!m_img_part || m_img_size < 8)
        return ERR_IMAGE;
    // Pad with 0xFF to whole chunks, as the probe's tools/fwupdate.py does (the CRC covers the padding).
    uint8_t  ff[BOOT_CHUNK];
    uint32_t pad = (BOOT_CHUNK - (m_img_size % BOOT_CHUNK)) % BOOT_CHUNK;
    memset(ff, 0xFF, sizeof(ff));
    if (m_img_size + pad > APP_SIZE_MAX ||
        (pad && esp_partition_write(m_img_part, m_img_size, ff, pad) != ESP_OK))
        return ERR_IMAGE;
    m_img_size += pad;

    // Same sanity check the bootloader applies: stack pointer in RAM, Thumb reset vector in the image.
    uint32_t vec[2];
    if (esp_partition_read(m_img_part, 0, vec, sizeof(vec)) != ESP_OK)
        return ERR_IMAGE;
    if (vec[0] <= RAM_BASE || vec[0] > RAM_END || (vec[0] & 3) || !(vec[1] & 1) || vec[1] <= APP_BASE ||
        vec[1] >= APP_BASE + m_img_size)
    {
        ESP_LOGW(TAG, "Not a probe application image (sp %08lx pc %08lx)", (unsigned long)vec[0],
                 (unsigned long)vec[1]);
        return ERR_IMAGE;
    }

    uint8_t  buf[512];
    uint32_t crc = 0;
    for (uint32_t off = 0; off < m_img_size; off += sizeof(buf))
    {
        uint32_t n = std::min<uint32_t>(sizeof(buf), m_img_size - off);
        if (esp_partition_read(m_img_part, off, buf, n) != ESP_OK)
            return ERR_IMAGE;
        crc = crc32(crc, buf, n);
    }
    m_img_crc   = crc;
    m_img_ready = true;
    ESP_LOGI(TAG, "Probe image staged: %lu bytes, crc %08lx", (unsigned long)m_img_size, (unsigned long)crc);
    return 0;
}

bool ProbeManager::imageInfo(uint32_t &size, uint32_t &crc)
{
    size = m_img_size;
    crc  = m_img_crc;
    return m_img_ready;
}

// ---------------------------------------------------------------------------
// Jobs
// ---------------------------------------------------------------------------

ProbeManager::Job ProbeManager::job()
{
    Lock l(m_lock);
    return m_job;
}

void ProbeManager::jobProgress(uint32_t done, uint32_t total, const char *fmt, ...)
{
    Lock l(m_lock);
    m_job.done  = done;
    m_job.total = total;
    if (fmt)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(m_job.msg, sizeof(m_job.msg), fmt, ap);
        va_end(ap);
    }
}

int ProbeManager::startScan()
{
    bool expected = false;
    if (!m_job_running.compare_exchange_strong(expected, true))
        return ERR_BUSY;
    {
        Lock l(m_lock);
        m_job       = Job{};
        m_job.type  = JobType::SCAN;
        m_job.state = JobState::RUNNING;
        m_job.total = 247;
        snprintf(m_job.msg, sizeof(m_job.msg), "Scanning");
        m_job_req = JobType::SCAN;
    }
    xTaskNotifyGive(m_task);
    return 0;
}

int ProbeManager::startUpdate(uint8_t addr)
{
    if (!valid_addr(addr))
        return ModbusMaster::ERR_ARG;
    if (!m_img_ready)
        return ERR_IMAGE;
    bool expected = false;
    if (!m_job_running.compare_exchange_strong(expected, true))
        return ERR_BUSY;
    {
        Lock l(m_lock);
        m_job       = Job{};
        m_job.type  = JobType::UPDATE;
        m_job.state = JobState::RUNNING;
        m_job.addr  = addr;
        m_job.total = m_img_size;
        snprintf(m_job.msg, sizeof(m_job.msg), "Starting");
        m_job_req = JobType::UPDATE;
    }
    xTaskNotifyGive(m_task);
    return 0;
}

void ProbeManager::runScan()
{
    std::vector<ScanHit> hits;
    for (int a = 1; a <= 247; a++)
    {
        uint16_t v[2];
        int      e = m_bus.readInput(a, IN_STATUS, 2, v, 100);  // answered by application and bootloader
        if (e == 0)
            hits.push_back({(uint8_t)a, v[1], is_boot_version(v[1])});
        else if (e > 0)
            hits.push_back({(uint8_t)a, 0, false});  // something answers, but not like a probe
        if ((a & 7) == 0 || e >= 0)
        {
            Lock l(m_lock);
            m_job.done = a;
            m_job.hits = hits;
        }
    }
    Lock l(m_lock);
    m_job.done  = 247;
    m_job.hits  = hits;
    m_job.state = JobState::DONE;
    snprintf(m_job.msg, sizeof(m_job.msg), "Found %d device(s)", (int)hits.size());
    ESP_LOGI(TAG, "%s", m_job.msg);
}

int ProbeManager::waitBootloader(uint8_t addr, int timeout_ms)
{
    int64_t end = now_us() + (int64_t)timeout_ms * 1000;
    while (now_us() < end)
    {
        uint16_t ver;
        if (m_bus.readInput(addr, IN_VERSION, 1, &ver, 150) == 0 && is_boot_version(ver))
            return 0;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ModbusMaster::ERR_TIMEOUT;
}

static const char *fw_err_str(uint16_t st)
{
    switch (st & 0xFF)
    {
        case 1:  return "erase failed";
        case 2:  return "image too large";
        case 3:  return "wrong state";
        case 4:  return "program failed";
        case 5:  return "flash not erased";
        case 6:  return "CRC mismatch";
        case 7:  return "not a probe application";
        default: return "error";
    }
}

int ProbeManager::update(uint8_t addr)
{
    const uint32_t size = m_img_size, crc = m_img_crc;

    // 1. Get the probe into its bootloader.
    uint16_t ver;
    int      e = m_bus.readInput(addr, IN_VERSION, 1, &ver, 300);
    if (e == 0 && !is_boot_version(ver))
    {
        if (ver < FW_FIRST_WITH_BOOTLOADER)
            return ERR_NO_BL;
        jobProgress(0, size, "Restarting probe into its bootloader");
        if ((e = m_bus.writeSingle(addr, HR_CMD, CMD_BOOTLOADER, 300)) != 0)
            return e;
        vTaskDelay(pdMS_TO_TICKS(300));
        if ((e = waitBootloader(addr, 3000)) != 0)
            return e;
    }
    else if (e != 0)
    {
        // No answer: a broken application keeps being reset by the probe's watchdog (or the probe
        // is being power-cycled). The bootloader stays when it gets "stay" within 200 ms of reset.
        jobProgress(0, size, "Probe not answering, waiting for its bootloader (up to 20 s)");
        ESP_LOGI(TAG, "probe %d not answering, trying to catch its bootloader", addr);
        int64_t end = now_us() + 20LL * 1000000;
        e           = ModbusMaster::ERR_TIMEOUT;
        while (now_us() < end && e != 0)
            e = m_bus.writeSingle(addr, BREG_CMD, BCMD_STAY, 40);
        if (e)
            return e;
    }

    // 2. Erase the whole application area (24 KB).
    jobProgress(0, size, "Erasing");
    if ((e = m_bus.writeSingle(addr, BREG_CMD, BCMD_ERASE, 3000)) != 0)
        return e;

    // 3. Chunks of 128 bytes. Re-sending a chunk is harmless, so transport errors are retried.
    uint8_t  buf[BOOT_CHUNK];
    uint16_t regs[2 + BOOT_CHUNK / 2];
    for (uint32_t off = 0; off < size; off += BOOT_CHUNK)
    {
        if (esp_partition_read(m_img_part, off, buf, BOOT_CHUNK) != ESP_OK)
            return ERR_IMAGE;
        regs[0] = (uint16_t)(off >> 16);
        regs[1] = (uint16_t)off;
        for (uint32_t i = 0; i < BOOT_CHUNK / 2; i++)
            regs[2 + i] = (uint16_t)(buf[2 * i] << 8 | buf[2 * i + 1]);
        for (int tries = 0; tries < 4; tries++)
        {
            e = m_bus.writeMultiple(addr, BREG_CHUNK, (uint16_t)(2 + BOOT_CHUNK / 2), regs, 1000);
            if (e >= 0)
                break;
        }
        if (e)
            return e;
        jobProgress(off + BOOT_CHUNK, size, "Writing");
    }

    // 4. Length + CRC, then commit: the probe checks the CRC and only then writes the reset vector.
    jobProgress(size, size, "Verifying");
    uint16_t lc[4] = {(uint16_t)(size >> 16), (uint16_t)size, (uint16_t)(crc >> 16), (uint16_t)crc};
    for (int tries = 0; tries < 4; tries++)
        if ((e = m_bus.writeMultiple(addr, BREG_LEN_HI, 4, lc, 500)) >= 0)
            break;
    if (e)
        return e;
    if ((e = m_bus.writeSingle(addr, BREG_CMD, BCMD_COMMIT, 2000)) != 0)
    {
        uint16_t st = 0;
        if (e > 0 && m_bus.readHolding(addr, BREG_STATUS, 1, &st, 300) == 0 && (st & FW_ERROR))
        {
            jobProgress(size, size, "Commit failed (%s)", fw_err_str(st));
            return ERR_COMMIT;
        }
        return e;
    }

    // 5. Start the new application and check that it answers.
    jobProgress(size, size, "Starting new firmware");
    if ((e = m_bus.writeSingle(addr, BREG_CMD, BCMD_RUN, 500)) != 0)
        return e;
    vTaskDelay(pdMS_TO_TICKS(500));
    for (int i = 0; i < 25; i++)
    {
        if (m_bus.readInput(addr, IN_VERSION, 1, &ver, 200) == 0 && !is_boot_version(ver))
        {
            jobProgress(size, size, "Updated, firmware %d.%d", ver >> 8, ver & 0xFF);
            if (on_event)
                on_event(Event::FW_UPDATED, addr, ver);
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return ERR_VERIFY;
}

void ProbeManager::runUpdate(uint8_t addr)
{
    ESP_LOGI(TAG, "Updating probe %d (%lu bytes)", addr, (unsigned long)m_img_size);
    int  e = update(addr);
    Lock l(m_lock);
    if (e == 0)
    {
        m_job.state = JobState::DONE;
        ESP_LOGI(TAG, "Probe %d: %s", addr, m_job.msg);
    }
    else
    {
        m_job.state = JobState::FAILED;
        char step[sizeof(m_job.msg)];
        snprintf(step, sizeof(step), "%s", m_job.msg);
        snprintf(m_job.msg, sizeof(m_job.msg), "%.48s: %s", step, errStr(e));
        ESP_LOGE(TAG, "Probe %d update failed: %s", addr, m_job.msg);
    }
}

const char *ProbeManager::errStr(int err)
{
    switch (err)
    {
        case ERR_EXISTS:    return "address already in use";
        case ERR_FULL:      return "probe list full";
        case ERR_BUSY:      return "busy with a scan or update";
        case ERR_NOT_FOUND: return "no such probe";
        case ERR_NO_BL:     return "probe firmware 1.0 has no bootloader (flash 1.1 + bootloader over SWD once)";
        case ERR_IMAGE:     return "no valid probe firmware image";
        case ERR_VERIFY:    return "no answer from the new firmware";
        case ERR_COMMIT:    return "image rejected by the probe, it stays in its bootloader";
        default:            return ModbusMaster::errStr(err);
    }
}
