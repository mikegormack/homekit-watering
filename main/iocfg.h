#pragma once

#define I2C_SCL_PIN         GPIO_NUM_4
#define I2C_SDA_PIN         GPIO_NUM_5

#define BTN_1_IOEXP_PIN     8
#define BTN_2_IOEXP_PIN     9
#define BTN_3_IOEXP_PIN     10
#define BTN_4_IOEXP_PIN     11

#define BTN_1_IOEXP_MASK    (1 << BTN_1_IOEXP_PIN)
#define BTN_2_IOEXP_MASK    (1 << BTN_2_IOEXP_PIN)
#define BTN_3_IOEXP_MASK    (1 << BTN_3_IOEXP_PIN)
#define BTN_4_IOEXP_MASK    (1 << BTN_4_IOEXP_PIN)