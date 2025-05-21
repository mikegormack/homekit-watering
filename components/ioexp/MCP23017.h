#pragma once

#include <driver/i2c.h>

#include <functional>

#define MCP23017_BASE_ADDRESS   0x20

// registers
#define MCP23017_IODIRA     0x00    // pin direction (0 = output, 1 = input)
#define MCP23017_IPOLA      0x02    // input polarity inversion (0 = non-inverted, 1 = inverted)
#define MCP23017_GPINTENA   0x04    // enable interrupt on change (0 = disable, 1 = enable)
#define MCP23017_DEFVALA    0x06    // default value for int on change
#define MCP23017_INTCONA    0x08    // int on change config (0 = int on change from previous, 1 = int on change from DEFVAL)
#define MCP23017_IOCONA     0x0A    // chip config
#define MCP23017_GPPUA      0x0C    // pull up (0 = disabled, 1 = enabled)
#define MCP23017_INTFA      0x0E    // int flag (read only)
#define MCP23017_INTCAPA    0x10    // int capture value (port value at time of interrupt)
#define MCP23017_GPIOA      0x12    // port value (read input / write output)
#define MCP23017_OLATA      0x14    // output latch value (set by GPIO write)

#define MCP23017_IODIRB     0x01
#define MCP23017_IPOLB      0x03
#define MCP23017_GPINTENB   0x05
#define MCP23017_DEFVALB    0x07
#define MCP23017_INTCONB    0x09
#define MCP23017_IOCONB     0x0B
#define MCP23017_GPPUB      0x0D
#define MCP23017_INTFB      0x0F
#define MCP23017_INTCAPB    0x11
#define MCP23017_GPIOB      0x13
#define MCP23017_OLATB      0x15

// define IOCON flags

#define IOCON_BANK          (1 << 7)    // address banking (0 = same bank, different banks)
#define IOCON_MIRROR        (1 << 6)    // int mirror (0 = disabled, 1 = enabled)
#define IOCON_SEQOP         (1 << 5)    // sequential addressing (0 = enabled, 1 = disabled)
#define IOCON_DISSLW        (1 << 4)    // SDA slew rate (0 = ENABLED, 1 = DISABLED)
#define IOCON_HAEN          (1 << 3)    // MCP23S17 hardware addr pin enable
#define IOCON_ODR           (1 << 2)    // int pin open drain (1 - open drain, 0 = push-pull)
#define IOCON_INTPOL        (1 << 1)    // int pin polarity (0 = active low, 1 = active high)

class MCP23017
{
public:
    MCP23017(i2c_port_t i2c_port, uint8_t address, int res_pin, int inta_pin, int intb_pin, bool int_mirror);
    ~MCP23017();

	/*********************************************************************
	 * @brief initialise the esp io and chip. performs a reset
	 *
	 * @return true = success
	 */
    bool init();

    void uninit();

	/*********************************************************************
	 * @brief perform pin reset of chip
	 *
	 * @return true = success
	 */
    bool reset();

	/*********************************************************************
	 * @brief Set event callback for interrupt
	 *
	 * @param callback
	 * @param user_data user context data returned in callback
	 */
	void setEventCallback(std::function<void(uint16_t, uint16_t, void*)> callback, void* user_data);

	/*********************************************************************
	 * @brief set IO direction for both ports
	 *
	 * @param iodir 0 = output, 1 = input
	 * @return true = success
	 */
    bool setIODIR(uint16_t iodir);

    /*********************************************************************
     * @brief get IO direction for both ports
     *
     * @param iodir 0 = output, 1 = input
	 * @return true = success
     */
	bool getIODIR(uint16_t* iodir);

	/*********************************************************************
	 * @brief set GPIO output value
	 *
	 * @param gpio
	 * @return true = success
	 */
	bool setGPIO(uint16_t gpio);
	bool getGPIO(uint16_t* gpio);

	bool setPullUp(uint16_t gpio);
	bool getPullUp(uint16_t* gpio);

	bool setIntEna(uint16_t mask);
	bool getIntEna(uint16_t* mask);

	bool setIntDefaultEnable(uint16_t enable);
	bool getIntDefaultEnable(uint16_t* enable);

	bool setIntDefaultValue(uint16_t val);
	bool getIntDefaultValue(uint16_t* val);

	bool getIntFlag(uint16_t* val);

	bool getIntCapture(uint16_t* val);

	bool setIntPinConfig(bool mirror, bool open_drain, bool act_hi);

	void regDump();


private:
	i2c_port_t m_i2c_port;
	uint8_t m_address;
	int m_res_pin;

	struct mcp23017_int_ctx_s
	{
		MCP23017* drv;
		int gpio;
	} m_inta_ctx, m_intb_ctx;
	bool m_int_mirror;

	QueueHandle_t m_int_evt_queue = NULL;

	std::function<void(uint16_t, uint16_t, void*)> m_callback;
	void* m_user_data;

	bool readRegister(uint8_t reg_addr, uint8_t *val);
	bool writeRegister(uint8_t reg_addr, uint8_t val);

	bool readRegister16(uint8_t reg_addr, uint16_t *val);
	bool writeRegister16(uint8_t reg_addr, uint16_t val);

	bool configIntPin(struct mcp23017_int_ctx_s* ctx);

	static void IRAM_ATTR isrHandler(void *arg);
	static void intTask(void *arg);
};

#define MCP23017_ADDRESS 0x20


//constants set up to emulate Arduino pin parameters
#define HIGH 1
#define LOW 0

#define INPUT 1
#define OUTPUT 0

#define CHANGE 0
#define FALLING 1
#define RISING 2

