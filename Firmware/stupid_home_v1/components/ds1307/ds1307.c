#include "ds1307.h"

static uint8_t binary_to_bcd(uint8_t value)
{
	uint8_t m, n;
	uint8_t bcd;
	bcd = value;
	if(value >= 10)
	{
		m = value / 10;
		n = value % 10;
		bcd = (m << 4) | n;
	}
	return bcd;
}

static uint8_t bcd_to_binary(uint8_t value)
{
	uint8_t m, n;
	m = (uint8_t)((value >> 4) * 10);
	n = value & (uint8_t)0x0F;
	return (m + n);
}

esp_err_t i2c_write_ds1307(i2c_port_t i2c_num, uint8_t value, uint8_t ds1307_addr)
{
    uint8_t tx[2] = {0};
	tx[0] = ds1307_addr;
	tx[1] = value;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDRESS << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write(cmd, tx, 2, ACK_CHECK_EN); 
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

uint8_t i2c_read_ds1307(i2c_port_t i2c_num, uint8_t ds1307_addr)
{
    uint8_t data;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS1307_ADDRESS << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, ds1307_addr, ACK_CHECK_EN);
	i2c_master_stop(cmd);
	esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS);
	i2c_cmd_link_delete(cmd);
	cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (DS1307_ADDRESS << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, &data, NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return data;
}

void ds1307_get_current_date(i2c_port_t i2c_num, RTC_date_t *rtc_date)
{
	rtc_date->day = bcd_to_binary(i2c_read_ds1307(i2c_num, (uint8_t)DS1307_ADDR_DAY));
	rtc_date->date = bcd_to_binary(i2c_read_ds1307(i2c_num, (uint8_t)DS1307_ADDR_DATE));
	rtc_date->month = bcd_to_binary(i2c_read_ds1307(i2c_num, (uint8_t)DS1307_ADDR_MONTH));
	rtc_date->year = bcd_to_binary(i2c_read_ds1307(i2c_num, (uint8_t)DS1307_ADDR_YEAR));
}

void ds1307_get_current_time(i2c_port_t i2c_num, RTC_time_t *rtc_time)
{
	uint8_t seconds, hrs;
	seconds = i2c_read_ds1307(i2c_num, DS1307_ADDR_SEC);
	seconds &= ~(1 << 7);
	rtc_time->seconds = bcd_to_binary(seconds);
	rtc_time->minutes = bcd_to_binary(i2c_read_ds1307(i2c_num, DS1307_ADDR_MIN));
	hrs = i2c_read_ds1307(i2c_num, DS1307_ADDR_HRS);
	if(hrs & (1 << 6))
	{
		//12-hour format
		rtc_time->time_format = !((hrs & (1 << 5)) == 0);
		hrs &= ~(0x3 << 5); // Clear 6 and 5
	}
	else
	{
		//24-hour format
		rtc_time->time_format = TIME_FORMAT_24HRS;
	}
	rtc_time->hours = bcd_to_binary(hrs);
}

void ds1307_set_current_date(i2c_port_t i2c_num, RTC_date_t *rtc_date)
{
	i2c_write_ds1307(i2c_num, binary_to_bcd(rtc_date->date), DS1307_ADDR_DATE);
	i2c_write_ds1307(i2c_num, binary_to_bcd(rtc_date->month) ,DS1307_ADDR_MONTH);
	i2c_write_ds1307(i2c_num, binary_to_bcd(rtc_date->year), DS1307_ADDR_YEAR);
	i2c_write_ds1307(i2c_num, binary_to_bcd(rtc_date->day), DS1307_ADDR_DAY);
}

void ds1307_set_current_time(i2c_port_t i2c_num, RTC_time_t *rtc_time)
{
	uint8_t seconds, hrs;
	seconds = binary_to_bcd(rtc_time->seconds);
	seconds &= ~(1 << 7);
	i2c_write_ds1307(i2c_num, seconds, DS1307_ADDR_SEC);
	i2c_write_ds1307(i2c_num, binary_to_bcd(rtc_time->minutes), DS1307_ADDR_MIN);
	hrs = binary_to_bcd(rtc_time->hours);
	if(rtc_time->time_format == TIME_FORMAT_24HRS){
		hrs &= ~(1 << 6);
	}
	else
	{
		hrs |= (1 << 6);
		hrs = (rtc_time->time_format == TIME_FORMAT_12HRS_PM) ? hrs | (1 << 5) :  hrs & ~( 1 << 5);
	}
	i2c_write_ds1307(i2c_num, hrs, DS1307_ADDR_HRS);
}

char *get_day_of_week(uint8_t count)
{
	char* days[] = { "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
	return days[count - 1];
}

void number_to_string(uint8_t num, char *buf)
{
	if(num < 10)
	{
		buf[0] = '0';
		buf[1] = num + 48;
	}
	else if(num >= 10 && num < 99)
	{
		buf[0] = (num / 10) + 48;
		buf[1] = (num % 10) + 48;
	}
}

char *time_to_string(RTC_time_t *rtc_time)
{ //00:00:00
	static char buf[9];
	buf[2] = ':';
	// buf[5] = ':';
	buf[5] = '\0';
	number_to_string(rtc_time->hours, buf);
	number_to_string(rtc_time->minutes, &buf[3]);
	// number_to_string(rtc_time->seconds, &buf[6]);
	// buf[8] = '\0';
	return buf;
}

char *date_to_string(RTC_date_t *rtc_date)
{
	static char buf[9];
	buf[2]= '/';
	buf[5]= '/';
	number_to_string(rtc_date->date, buf);
	number_to_string(rtc_date->month, &buf[3]);
	number_to_string(rtc_date->year, &buf[6]);
	buf[8]= '\0';
	return buf;
}