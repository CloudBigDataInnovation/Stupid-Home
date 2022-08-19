#ifndef _DS1307_H_
#define _DS1307_H_

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

#define DS1307_ADDR_SEC 		    0x00
#define DS1307_ADDR_MIN 		    0x01
#define DS1307_ADDR_HRS			    0x02
#define DS1307_ADDR_DAY			    0x03
#define DS1307_ADDR_DATE		    0x04
#define DS1307_ADDR_MONTH		    0x05
#define DS1307_ADDR_YEAR		    0x06

#define TIME_FORMAT_12HRS_AM        0
#define TIME_FORMAT_12HRS_PM        1
#define TIME_FORMAT_24HRS           2

#define DS1307_ADDRESS              0x68

#define SUNDAY                      1
#define MONDAY                      2
#define TUESDAY                     3
#define WEDNESDAY                   4
#define THURSDAY                    5
#define FRIDAY                      6
#define SATURDAY                    7

typedef struct 
{
    uint8_t date;
    uint8_t month;
    uint8_t year;
    uint8_t day;
}RTC_date_t;

typedef struct 
{
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t time_format;
}RTC_time_t;

void ds1307_get_current_date(i2c_port_t i2c_num, RTC_date_t *rtc_date);
void ds1307_get_current_time(i2c_port_t i2c_num, RTC_time_t *rtc_time);
void ds1307_set_current_date(i2c_port_t i2c_num, RTC_date_t *rtc_date);
void ds1307_set_current_time(i2c_port_t i2c_num, RTC_time_t *rtc_time);
char *time_to_string(RTC_time_t *rtc_time);
char *date_to_string(RTC_date_t *rtc_date);

#endif
