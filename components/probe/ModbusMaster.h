#pragma once

#include <stdint.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Modbus RTU master on a half-duplex RS485 UART (driver enable on the UART RTS pin).
// All calls are thread-safe; one transaction runs on the bus at a time.
class ModbusMaster
{
public:
    struct Config
    {
        uart_port_t uart;
        int         tx_gpio;
        int         rx_gpio;
        int         de_gpio;  // RS485 DE (and /RE if tied), driven by the UART as RTS
        uint32_t    baud;
    };

    // Return values: 0 = ok, 1..255 = Modbus exception code from the slave, < 0 = transport error.
    static constexpr int ERR_TIMEOUT  = -1;
    static constexpr int ERR_CRC      = -2;
    static constexpr int ERR_FRAME    = -3;  // wrong address / function / length in the reply
    static constexpr int ERR_NOT_INIT = -4;
    static constexpr int ERR_ARG      = -5;

    static constexpr int DEFAULT_TIMEOUT_MS = 200;

    ModbusMaster();
    ~ModbusMaster();

    bool begin(const Config &cfg);
    bool ready() const { return m_ready; }

    int readInput(uint8_t addr, uint16_t reg, uint16_t n, uint16_t *out, int timeout_ms = DEFAULT_TIMEOUT_MS);
    int readHolding(uint8_t addr, uint16_t reg, uint16_t n, uint16_t *out, int timeout_ms = DEFAULT_TIMEOUT_MS);
    int writeSingle(uint8_t addr, uint16_t reg, uint16_t val, int timeout_ms = DEFAULT_TIMEOUT_MS);
    int writeMultiple(uint8_t addr, uint16_t reg, uint16_t n, const uint16_t *vals,
                      int timeout_ms = DEFAULT_TIMEOUT_MS);

    static uint16_t    crc16(const uint8_t *data, int len);
    static const char *errStr(int err);

private:
    Config            m_cfg{};
    bool              m_ready = false;
    SemaphoreHandle_t m_lock  = nullptr;

    int readRegs(uint8_t fc, uint8_t addr, uint16_t reg, uint16_t n, uint16_t *out, int timeout_ms);
    // Sends req (CRC appended here), receives the reply into rsp; returns reply length or < 0.
    int transact(uint8_t *req, int len, uint8_t *rsp, int rsp_max, int timeout_ms);
};
