#pragma once

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include <functional>
#include <vector>

#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "ModbusMaster.h"

// Register map of the soil-moisture-probe firmware and bootloader (that repository's
// firmware/README.md, src/boot/boot_regs.h and src/memmap.h).
namespace probe_reg
{
// Input registers (application)
constexpr uint16_t IN_SENSE = 0, IN_REF = 1, IN_NTC = 2, IN_VREFINT = 3, IN_RATIO = 4, IN_MOISTURE = 5, IN_TEMP = 6,
                   IN_VDDA = 7, IN_STATUS = 8, IN_VERSION = 9, IN_COUNT = 10, IN_MOISTURE_RAW = 11;
constexpr uint16_t IN_NUM = 12;

// Version register: application 0x0xxx, bootloader 0xB0xx (which only answers input registers 8 and 9).
constexpr uint16_t VER_BOOT_MASK = 0xF000, VER_BOOT = 0xB000;
constexpr bool     is_boot_version(uint16_t v) { return (v & VER_BOOT_MASK) == VER_BOOT; }
constexpr uint16_t FW_FIRST_WITH_BOOTLOADER = 0x0101;  // 1.1 runs from 0x08001800 under the bootloader

// Status bits (input register 8, application)
constexpr uint16_t ST_NTC_FAULT = 0x0001, ST_UNCALIBRATED = 0x0002, ST_CFG_DEFAULTS = 0x0004, ST_REF_LOW = 0x0008,
                   ST_SAVE_FAILED = 0x0010;

// Holding registers (application)
constexpr uint16_t HR_ADDR = 0, HR_BAUD = 1, HR_PARITY = 2, HR_CAL_DRY = 3, HR_CAL_WET = 4, HR_TC_COEF = 5,
                   HR_TC_REF = 6, HR_FLAGS = 7, HR_MCO_DIV = 8, HR_SETTLE = 9, HR_INTERVAL = 10, HR_CMD = 11;
constexpr uint16_t FLAG_TEMP_COMP = 0x0001;

// Commands (holding register 11)
constexpr uint16_t CMD_SAVE = 1, CMD_REBOOT = 2, CMD_CAPTURE_DRY = 3, CMD_CAPTURE_WET = 4, CMD_DEFAULTS = 5,
                   CMD_BOOTLOADER = 6;

// Bootloader holding registers
constexpr uint16_t BREG_CMD = 0x100, BREG_STATUS = 0x101, BREG_LEN_HI = 0x102, BREG_CHUNK = 0x200;
constexpr uint16_t BCMD_ERASE = 1, BCMD_COMMIT = 2, BCMD_RUN = 3, BCMD_STAY = 4;
constexpr uint32_t BOOT_CHUNK = 128;  // bytes per chunk: FC 16 of 2 offset + 64 data registers

// Bootloader status (input register 8 / holding 0x101 while in the bootloader)
constexpr uint16_t FW_IDLE = 0, FW_ERASED = 1, FW_COMMITTED = 2, FW_ERROR = 0x8000;

// Application image (src/memmap.h)
constexpr uint32_t APP_BASE = 0x08001800, APP_SIZE_MAX = 24 * 1024, APP_END = APP_BASE + APP_SIZE_MAX;
constexpr uint32_t RAM_BASE = 0x20000000, RAM_END = 0x20002000;
}  // namespace probe_reg

class ProbeManager
{
public:
    static constexpr int MAX_PROBES = 8;
    static constexpr int NAME_LEN   = 16;

    // Errors beyond ModbusMaster's (which are passed through unchanged).
    static constexpr int ERR_EXISTS    = -10;
    static constexpr int ERR_FULL      = -11;
    static constexpr int ERR_BUSY      = -12;
    static constexpr int ERR_NOT_FOUND = -13;
    static constexpr int ERR_NO_BL     = -14;
    static constexpr int ERR_COMMIT    = -17;
    static constexpr int ERR_IMAGE     = -15;
    static constexpr int ERR_VERIFY    = -16;

    struct Reading
    {
        bool     valid         = false;  // at least one good poll since boot
        bool     online        = false;  // answering (fewer than OFFLINE_AFTER failed polls in a row)
        bool     in_bootloader = false;
        int64_t  last_ok_us    = 0;
        uint8_t  fails         = 0;
        uint16_t sense = 0, ref = 0, ntc = 0, vrefint = 0, ratio = 0, vdda_mv = 0, status = 0, count = 0;
        int16_t  moisture = 0, moisture_raw = 0, temp = 0;  // 0.01 % / 0.01 degC
        uint16_t fw_version = 0;   // application version, 0x0101 = 1.1 (last seen)
        int32_t  bl_version = -1;  // bootloader version register (0xB0xx) when last seen in it, -1 unknown
        uint16_t bl_status  = 0;   // bootloader update status while in_bootloader
    };

    struct Probe
    {
        uint8_t addr;
        char    name[NAME_LEN];
        Reading r;
    };

    // Holding registers 0..10.
    struct Settings
    {
        uint16_t slave_addr, baud_100, parity, cal_dry, cal_wet;
        int16_t  tc_coef, tc_ref;
        uint16_t flags, mco_div, settle_ms, interval_ms;
    };

    enum class JobType  { NONE, SCAN, UPDATE };
    enum class JobState { IDLE, RUNNING, DONE, FAILED };

    struct ScanHit
    {
        uint8_t  addr;
        uint16_t fw_version;
        bool     in_bootloader;
    };

    struct Job
    {
        JobType  type  = JobType::NONE;
        JobState state = JobState::IDLE;
        uint8_t  addr  = 0;  // update target / scan position
        uint32_t done  = 0, total = 0;
        char     msg[128] = "";
        std::vector<ScanHit> hits;
    };

    enum class Event { OFFLINE, ONLINE, FW_UPDATED };

    using ReadingFn   = std::function<void(const Probe &)>;
    using EventFn     = std::function<void(Event, uint8_t addr, uint16_t val)>;
    using ReaddressFn = std::function<void(uint8_t old_addr, uint8_t new_addr)>;

    ReadingFn   on_reading;    // after every poll of a probe (probe task)
    EventFn     on_event;      // online/offline transitions, firmware updated (val = new version)
    ReaddressFn on_readdress;  // a probe's Modbus address was changed

    ProbeManager();

    // Loads the probe list from NVS (namespace "storage") and starts the bus and the poll task.
    bool begin(const ModbusMaster::Config &bus, uint32_t poll_interval_s);
    // Loads only the list (usable before begin(), e.g. to build HomeKit services).
    void load();

    std::vector<Probe> list();
    bool               get(uint8_t addr, Probe &out);
    // Moisture in % for watering decisions, or < 0 when unknown: not configured, offline, stale,
    // uncalibrated or in the bootloader.
    float moisture(uint8_t addr);
    // Soil temperature in degC; false under the same conditions, or with an NTC fault.
    bool temperature(uint8_t addr, float *temp_c);

    int  add(uint8_t addr, const char *name);
    int  remove(uint8_t addr);
    int  rename(uint8_t addr, const char *name);
    void pollNow();

    // Direct operations, blocking for one or a few Modbus transactions. ERR_BUSY while a job runs.
    int readSettings(uint8_t addr, Settings &s);
    int writeCalibration(uint8_t addr, const Settings &s, bool save);  // holding registers 3..10
    int command(uint8_t addr, uint16_t cmd);
    int changeAddress(uint8_t old_addr, uint8_t new_addr);

    // Probe firmware image, staged in the inactive OTA partition (unused unless an ESP32 OTA runs).
    int  imageBegin(size_t size);
    int  imageWrite(const void *data, size_t len);
    int  imageEnd();  // pads to the 128-byte chunk size, checks the vectors, computes the CRC
    bool imageInfo(uint32_t &size, uint32_t &crc);

    int  startScan();
    int  startUpdate(uint8_t addr);
    Job  job();
    bool busy() const { return m_job_running; }

    ModbusMaster &bus() { return m_bus; }

    static const char *errStr(int err);
    static uint32_t    crc32(uint32_t crc, const uint8_t *data, size_t len);

private:
    static constexpr int OFFLINE_AFTER = 3;

    ModbusMaster      m_bus;
    SemaphoreHandle_t m_lock;
    TaskHandle_t      m_task = nullptr;
    uint32_t          m_poll_ms = 10000;
    std::vector<Probe> m_probes;
    std::atomic<bool> m_poll_now{false};

    // job
    Job               m_job;
    std::atomic<bool> m_job_running{false};
    JobType           m_job_req = JobType::NONE;

    // staged image
    const esp_partition_t *m_img_part  = nullptr;
    uint32_t               m_img_size  = 0;  // bytes received / padded size once complete
    uint32_t               m_img_crc   = 0;
    bool                   m_img_ready = false;

    void save();
    int  pollOne(uint8_t addr, Reading &r);
    void pollAll();
    void runScan();
    void runUpdate(uint8_t addr);
    int  update(uint8_t addr);
    void jobProgress(uint32_t done, uint32_t total, const char *fmt, ...);
    int  waitBootloader(uint8_t addr, int timeout_ms);

    static void task_entry(void *arg);
};
