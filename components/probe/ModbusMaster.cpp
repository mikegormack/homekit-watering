#include "ModbusMaster.h"

#include <string.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <sdkconfig.h>
#if CONFIG_IDF_TARGET_ESP32
#include <esp_efuse.h>
#include <soc/efuse_defs.h>
#endif
#include <freertos/task.h>

static const char *TAG = "Modbus";

static constexpr int MAX_FRAME = 256;

ModbusMaster::ModbusMaster() : m_lock(xSemaphoreCreateMutex()) {}

ModbusMaster::~ModbusMaster()
{
    if (m_ready)
        uart_driver_delete(m_cfg.uart);
    if (m_lock)
        vSemaphoreDelete(m_lock);
}

// Why a GPIO can't be used for the bus, or nullptr if it can. Taking over a flash or console pin
// stops the chip (code fetch fails) or the log, and ends in a watchdog reset with no message.
static const char *pin_problem(int gpio, bool output)
{
    if (!GPIO_IS_VALID_GPIO(gpio))
        return "not a GPIO";
    if (output && !GPIO_IS_VALID_OUTPUT_GPIO(gpio))
        return "input-only (GPIO34-39)";
#if CONFIG_IDF_TARGET_ESP32
    if (gpio >= 6 && gpio <= 11)
        return "wired to the SPI flash";
    uint32_t pkg = esp_efuse_get_pkg_ver();
    if ((gpio == 16 || gpio == 17) &&
        (pkg == EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4 || pkg == EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5 ||
         pkg == EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302))
        return "wired to the in-package flash/PSRAM on this chip (ESP32-PICO / D2WD)";
#endif
#if CONFIG_ESP_CONSOLE_UART_DEFAULT && CONFIG_ESP_CONSOLE_UART_NUM == 0
    if (gpio == 1 || gpio == 3)
        return "the serial console (UART0)";
#endif
    return nullptr;
}

bool ModbusMaster::begin(const Config &cfg)
{
    m_cfg = cfg;

    // Log the pins first, so a crash while they are switched over still shows them.
    ESP_LOGI(TAG, "RS485 on UART%d: tx=%d rx=%d de=%d", (int)cfg.uart, cfg.tx_gpio, cfg.rx_gpio, cfg.de_gpio);
    const struct { const char *name; int gpio; bool output; } pins[] = {
        {"TX", cfg.tx_gpio, true}, {"RX", cfg.rx_gpio, false}, {"DE", cfg.de_gpio, true}};
    for (const auto &p : pins)
    {
        const char *why = pin_problem(p.gpio, p.output);
        if (why)
        {
            ESP_LOGE(TAG, "RS485 %s pin GPIO%d can't be used: %s. Change it in menuconfig \"Soil probes (RS485)\"",
                     p.name, p.gpio, why);
            return false;
        }
    }

    uart_config_t uc = {};
    uc.baud_rate  = (int)cfg.baud;
    uc.data_bits  = UART_DATA_8_BITS;
    uc.parity     = UART_PARITY_DISABLE;
    uc.stop_bits  = UART_STOP_BITS_2;  // Modbus RTU: 2 stop bits without parity (probe default 8N2)
    uc.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    uc.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_driver_install(cfg.uart, MAX_FRAME * 2, 0, 0, nullptr, 0);
    if (err == ESP_OK)
        err = uart_param_config(cfg.uart, &uc);
    if (err == ESP_OK)
        err = uart_set_pin(cfg.uart, cfg.tx_gpio, cfg.rx_gpio, cfg.de_gpio, UART_PIN_NO_CHANGE);
    if (err == ESP_OK)
        err = uart_set_mode(cfg.uart, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "UART%d init failed: %s", (int)cfg.uart, esp_err_to_name(err));
        return false;
    }

    m_ready = true;
    ESP_LOGI(TAG, "RS485 ready, %lu 8N2", (unsigned long)cfg.baud);
    return true;
}

uint16_t ModbusMaster::crc16(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    while (len--)
    {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

const char *ModbusMaster::errStr(int err)
{
    switch (err)
    {
        case 0:            return "ok";
        case 1:            return "illegal function";
        case 2:            return "illegal address";
        case 3:            return "illegal value";
        case 4:            return "slave failure";
        case ERR_TIMEOUT:  return "no reply";
        case ERR_CRC:      return "CRC error";
        case ERR_FRAME:    return "bad reply";
        case ERR_NOT_INIT: return "RS485 not initialised";
        case ERR_ARG:      return "bad argument";
        default:           return err > 0 ? "exception" : "error";
    }
}

int ModbusMaster::transact(uint8_t *req, int len, uint8_t *rsp, int rsp_max, int timeout_ms)
{
    if (!m_ready)
        return ERR_NOT_INIT;

    uint16_t crc = crc16(req, len);
    req[len++]   = (uint8_t)crc;
    req[len++]   = (uint8_t)(crc >> 8);

    xSemaphoreTake(m_lock, portMAX_DELAY);

    // 1 tick (10 ms) of silence before every request covers the 3.5 character inter-frame gap.
    vTaskDelay(1);
    uart_flush_input(m_cfg.uart);
    uart_write_bytes(m_cfg.uart, req, len);
    uart_wait_tx_done(m_cfg.uart, pdMS_TO_TICKS(1000));
    uart_flush_input(m_cfg.uart);  // drop the local echo if the transceiver's receiver is not gated by DE

    int ret;
    // Header: address, function, and either the exception code or the first data byte.
    int n = uart_read_bytes(m_cfg.uart, rsp, 3, pdMS_TO_TICKS(timeout_ms));
    if (n < 3)
    {
        ret = n <= 0 ? ERR_TIMEOUT : ERR_FRAME;
    }
    else
    {
        int total;
        if (rsp[1] & 0x80)
            total = 5;
        else if (rsp[1] == 3 || rsp[1] == 4)
            total = 3 + rsp[2] + 2;
        else
            total = 8;  // FC 06 / 16 echo
        if (total > rsp_max)
            total = rsp_max;

        // Rest of the frame follows without gaps; allow some slack for slow slaves.
        int rest = total - 3;
        int got  = rest > 0 ? uart_read_bytes(m_cfg.uart, rsp + 3, rest, pdMS_TO_TICKS(50 + rest * 2)) : 0;
        if (got < rest)
            ret = ERR_FRAME;
        else if (crc16(rsp, total - 2) != (uint16_t)(rsp[total - 2] | rsp[total - 1] << 8))
            ret = ERR_CRC;
        else if (rsp[0] != req[0] || (rsp[1] & 0x7F) != req[1])
            ret = ERR_FRAME;
        else
            ret = total;
    }

    xSemaphoreGive(m_lock);

    if (ret < 0)
        ESP_LOGD(TAG, "addr %d fc %d: %s", req[0], req[1], errStr(ret));
    return ret;
}

int ModbusMaster::readRegs(uint8_t fc, uint8_t addr, uint16_t reg, uint16_t n, uint16_t *out, int timeout_ms)
{
    if (n < 1 || n > 125 || addr < 1 || addr > 247)
        return ERR_ARG;
    uint8_t req[8] = {addr, fc, (uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)(n >> 8), (uint8_t)n};
    uint8_t rsp[MAX_FRAME];
    int     len = transact(req, 6, rsp, sizeof(rsp), timeout_ms);
    if (len < 0)
        return len;
    if (rsp[1] & 0x80)
        return rsp[2] ? rsp[2] : ERR_FRAME;
    if (rsp[2] != n * 2 || len != 5 + n * 2)
        return ERR_FRAME;
    for (int i = 0; i < n; i++)
        out[i] = (uint16_t)(rsp[3 + 2 * i] << 8 | rsp[4 + 2 * i]);
    return 0;
}

int ModbusMaster::readInput(uint8_t addr, uint16_t reg, uint16_t n, uint16_t *out, int timeout_ms)
{
    return readRegs(4, addr, reg, n, out, timeout_ms);
}

int ModbusMaster::readHolding(uint8_t addr, uint16_t reg, uint16_t n, uint16_t *out, int timeout_ms)
{
    return readRegs(3, addr, reg, n, out, timeout_ms);
}

int ModbusMaster::writeSingle(uint8_t addr, uint16_t reg, uint16_t val, int timeout_ms)
{
    if (addr < 1 || addr > 247)
        return ERR_ARG;
    uint8_t req[8] = {addr, 6, (uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)(val >> 8), (uint8_t)val};
    uint8_t rsp[8];
    int     len = transact(req, 6, rsp, sizeof(rsp), timeout_ms);
    if (len < 0)
        return len;
    if (rsp[1] & 0x80)
        return rsp[2] ? rsp[2] : ERR_FRAME;
    return memcmp(rsp, req, 6) == 0 ? 0 : ERR_FRAME;
}

int ModbusMaster::writeMultiple(uint8_t addr, uint16_t reg, uint16_t n, const uint16_t *vals, int timeout_ms)
{
    if (n < 1 || n > 123 || addr < 1 || addr > 247)
        return ERR_ARG;
    uint8_t req[MAX_FRAME];
    req[0] = addr;
    req[1] = 16;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)reg;
    req[4] = (uint8_t)(n >> 8);
    req[5] = (uint8_t)n;
    req[6] = (uint8_t)(n * 2);
    for (int i = 0; i < n; i++)
    {
        req[7 + 2 * i] = (uint8_t)(vals[i] >> 8);
        req[8 + 2 * i] = (uint8_t)vals[i];
    }
    uint8_t rsp[8];
    int     len = transact(req, 7 + 2 * n, rsp, sizeof(rsp), timeout_ms);
    if (len < 0)
        return len;
    if (rsp[1] & 0x80)
        return rsp[2] ? rsp[2] : ERR_FRAME;
    return memcmp(rsp, req, 6) == 0 ? 0 : ERR_FRAME;
}
