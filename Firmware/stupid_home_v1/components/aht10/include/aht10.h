#ifndef _AHT10_H_
#define _AHT10_H_

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "driver/i2c.h"

#define GPIO_I2C_SDA            GPIO_NUM_21
#define GPIO_I2C_SCL            GPIO_NUM_22
#define I2C_CLOCK_FREQ          100000

#define ACK_CHECK_EN            0x1                        
#define ACK_CHECK_DIS           0x0 
#define ACK_VAL                 0x0                            
#define NACK_VAL                0x1

#define AHT10_ADDRESS           0x38
#define AHT10_CMD_MODE          0
#define AHT10_MEASURE_MODE      1

void read_data_aht10(i2c_port_t i2c_num, char *aht10_temp, char *aht10_hum);
void ftoa(float n, char* res, int afterpoint);

#endif