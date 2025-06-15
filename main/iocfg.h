#pragma once

#define I2C_SCL_PIN         GPIO_NUM_4
#define I2C_SDA_PIN         GPIO_NUM_5

#define BTN_SEL_IOEXP_PIN   8
#define BTN_BACK_IOEXP_PIN  9
#define BTN_UP_IOEXP_PIN    10
#define BTN_DN_IOEXP_PIN    11

#define BTN_SEL_ID          1
#define BTN_BACK_ID         2
#define BTN_UP_ID           3
#define BTN_DN_ID           4

#define BTN_SEL_IOEXP_MASK  (1 << BTN_SEL_IOEXP_PIN)
#define BTN_BACK_IOEXP_MASK (1 << BTN_BACK_IOEXP_PIN)
#define BTN_UP_IOEXP_MASK   (1 << BTN_UP_IOEXP_PIN)
#define BTN_DN_IOEXP_MASK   (1 << BTN_DN_IOEXP_PIN)

#define ARRAY_SIZE(x)       (sizeof(x)/sizeof(x[0]))