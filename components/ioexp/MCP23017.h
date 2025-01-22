#pragma once

#include <driver/i2c.h>

#define MCP23017_BASE_ADDRESS   0x20

// registers
#define MCP23017_IODIRA     0x00
#define MCP23017_IPOLA      0x02
#define MCP23017_GPINTENA   0x04
#define MCP23017_DEFVALA    0x06
#define MCP23017_INTCONA    0x08
#define MCP23017_IOCONA     0x0A
#define MCP23017_GPPUA      0x0C
#define MCP23017_INTFA      0x0E
#define MCP23017_INTCAPA    0x10
#define MCP23017_GPIOA      0x12
#define MCP23017_OLATA      0x14

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

class MCP23017
{
public:
    MCP23017(i2c_port_t i2c_port, uint8_t address);
    ~MCP23017();

    void pinMode(uint8_t p, uint8_t d);
    void digitalWrite(uint8_t p, uint8_t d);
    void pullUp(uint8_t p, uint8_t d);
    bool digitalRead(uint8_t p);

    void writeGPIOAB(uint16_t);
    uint16_t readGPIOAB();
    uint8_t readGPIO(uint8_t b);

    void setupInterrupts(uint8_t mirroring, uint8_t open, uint8_t polarity);
    void setupInterruptPin(uint8_t p, uint8_t mode);
    uint8_t getLastInterruptPin();
    uint8_t getLastInterruptPinValue();

private:
	i2c_port_t m_i2c_port;
	uint8_t m_address;

	bool readRegister(uint8_t reg_addr, uint8_t *val);
	bool writeRegister(uint8_t reg_addr, uint8_t val);

    uint8_t bitForPin(uint8_t pin);
    uint8_t regForPin(uint8_t pin, uint8_t portAaddr, uint8_t portBaddr);



    void updateRegisterBit(uint8_t p, uint8_t pValue, uint8_t portAaddr, uint8_t portBaddr);

    bool bitRead(uint8_t num, uint8_t index);
    void bitWrite(uint8_t &var, uint8_t index, uint8_t bit);
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

#endif
