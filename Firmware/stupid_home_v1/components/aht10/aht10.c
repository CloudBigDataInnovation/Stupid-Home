#include "aht10.h"

void reverse(char* str, int len)
{
    int i = 0, j = len - 1, temp;
    while (i < j) {
        temp = str[i];
        str[i] = str[j];
        str[j] = temp;
        i++;
        j--;
    }
}

int intToStr(int x, char str[], int d)
{
    int i = 0;
    if(x == 0)
    {
        str[i++] = '0';
    }
    while (x) {
        str[i++] = (x % 10) + '0';
        x = x / 10;
    }
    while (i < d)
        str[i++] = '0';
  
    reverse(str, i);
    str[i] = '\0';
    return i;
}

void ftoa(float n, char* res, int afterpoint)
{
    int ipart = (int)n;
    float fpart = n - (float)ipart;
    int i = intToStr(ipart, res, 0);
    if (afterpoint != 0) 
    {
        res[i] = '.';
        fpart = fpart * pow(10, afterpoint);
        intToStr((int)fpart, res + i + 1, afterpoint);
    }
}

esp_err_t i2c_write_aht10(i2c_port_t i2c_num)
{
    uint8_t aht10_cmd[3] = {0xAC, 0x33, 0x00};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AHT10_ADDRESS << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
    i2c_master_write(cmd, aht10_cmd, 3, ACK_CHECK_EN); 
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t i2c_read_aht10(i2c_port_t i2c_num, uint8_t* data_rd, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AHT10_ADDRESS << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
    if(size >  1)
    {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL); 
    }
    i2c_master_read_byte(cmd, data_rd, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

void read_data_aht10(i2c_port_t i2c_num, char *aht10_temp, char *aht10_hum)
{
    uint8_t aht10_rx_data[6] = {0};
    uint32_t aht10_temperature;
    uint32_t aht10_humidity;
    float temperature, humidity;
    i2c_write_aht10(i2c_num);
    i2c_read_aht10(i2c_num, aht10_rx_data, 6);
    if(~aht10_rx_data[0] & 0x80)
    {
        aht10_temperature = (((uint32_t)aht10_rx_data[3] & 15) << 16) | ((uint32_t)aht10_rx_data[4] << 8) | ((uint32_t)aht10_rx_data[5]);
        temperature = ((float)aht10_temperature * 200 / 1048576) - 50;
        aht10_humidity = ((uint32_t)aht10_rx_data[1] << 12) | ((uint32_t)aht10_rx_data[2] << 4) | ((uint32_t)aht10_rx_data[3] >> 4);
        humidity = (float)aht10_humidity * 100 / 1048576;
        ftoa(temperature, aht10_temp, 2);  
        ftoa(humidity, aht10_hum, 2);
    }
}

