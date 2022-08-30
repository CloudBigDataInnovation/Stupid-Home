#ifndef _LCD16X2_H_
#define _LCD16X2_H_

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "driver/i2c.h"

#define GPIO_I2C_SDA                GPIO_NUM_21
#define GPIO_I2C_SCL                GPIO_NUM_22
#define I2C_CLOCK_FREQ              100000

#define ACK_CHECK_EN                0x1                        
#define ACK_CHECK_DIS               0x0 
#define ACK_VAL                     0x0                            
#define NACK_VAL                    0x1

#define LCD_ADDRESS                 0x27

void lcd_init(i2c_port_t i2c_num);
void lcd_send_string(i2c_port_t i2c_num, char *str);
void lcd_clear(i2c_port_t i2c_num);
void lcd_send_data(i2c_port_t i2c_num, char data);
void lcd_send_cmd(i2c_port_t i2c_num, char cmd);

#endif
