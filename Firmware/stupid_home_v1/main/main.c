#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_wifi.h"
#include "esp_tls.h"
#include "esp_smartconfig.h"
#include "mqtt_client.h"
#include "esp_adc_cal.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/adc.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "aht10.h"
#include "ds1307.h"

#define HASH_LEN                    32
#define OTA_URL_SIZE                256
#define WIFI_CONNECTED_BIT          BIT0
#define SMART_CONFIG_DONE_BIT       BIT1
#define SMART_CONFIG_START_BIT      BIT2
#define MQTT_CONNECTED_BIT          BIT3
#define FIRMWARE_VERSION            0.3
#define RELAY_1_NUM                 GPIO_NUM_17
#define RELAY_2_NUM                 GPIO_NUM_16
#define RELAY_3_NUM                 GPIO_NUM_4
#define RELAY_4_NUM                 GPIO_NUM_0
#define WIFI_LED_NUM                GPIO_NUM_15
#define PUBLISH                     2
#define SUBSCRIBE                   3
#define RELAY_1_TOPIC               "vanperdung/relay_1"
#define RELAY_2_TOPIC               "vanperdung/relay_2"
#define RELAY_3_TOPIC               "vanperdung/relay_3"
#define RELAY_4_TOPIC               "vanperdung/relay_4"
#define HUMIDITY_TOPIC              "vanperdung/hum"
#define TEMPERATURE_TOPIC           "vanperdung/temp"
#define POWER_TOPIC                 "vanperdung/power"
#define DEFAULT_VREF                1149 
static const char *ota_tag = "OTA TASK";
static const char *i2c_sensor_tag = "SENSOR TASK";
static const char *wifi_tag = "WIFI";
static const char *smart_config_tag = "SMART CONFIG TASK";
static const char *smart_config_wifi_tag = "SMART CONFIG";
static const char *mqtt_node_red_tag = "MQTT_NODE_RED_TASK";
static const char *mqtt_tag = "MQTT";
static const char *adc_power_tag = "ADC POWER TASK";
extern const uint8_t github_cert_pem_start[] asm("_binary_git_ota_pem_start");
extern const uint8_t github_cert_pem_end[] asm("_binary_git_ota_pem_end");

TaskHandle_t ota_task_handle;
TaskHandle_t read_i2c_sensor_task_handle;
TaskHandle_t smart_config_task_handle;
TaskHandle_t mqtt_adafruit_task_handle;
TaskHandle_t adc_power_task_handle;
QueueHandle_t queue_sub_handle;
char version_json_data[512] = {0};
EventGroupHandle_t event_group;
int wifi_retry = 0;
typedef struct 
{
    int topic_type;
    int topic_len;
    char topic[64];
    int data_len;
    char data[64];
} mqtt_struct_t;

void mqtt_event_handler_cb(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_t *event = (esp_mqtt_event_t*)event_data;
    esp_mqtt_client_handle_t client = event->client;
    mqtt_struct_t mqtt;
    switch (event->event_id) 
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(mqtt_tag, "MQTT_EVENT_CONNECTED"); 
            esp_mqtt_client_subscribe(client, RELAY_1_TOPIC, 0);
            esp_mqtt_client_subscribe(client, RELAY_2_TOPIC, 0);
            esp_mqtt_client_subscribe(client, RELAY_3_TOPIC, 0);
            esp_mqtt_client_subscribe(client, RELAY_4_TOPIC, 0);
            xEventGroupSetBits(event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(mqtt_tag, "MQTT_EVENT_DISCONNECTED");
            xEventGroupClearBits(event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(mqtt_tag, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(mqtt_tag, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(mqtt_tag, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(mqtt_tag, "MQTT_EVENT_DATA");
            // ESP_LOGI(mqtt_tag, "TOPIC=%.*s\r\n", event->topic_len, event->topic);
            // ESP_LOGI(mqtt_tag, "DATA=%.*s\r\n", event->data_len, event->data);
            mqtt.topic_type = SUBSCRIBE;
            mqtt.topic_len = event->topic_len;
            memset(mqtt.topic, '\0', sizeof(mqtt.topic));
            memcpy(mqtt.topic, event->topic, event->topic_len);
            mqtt.data_len = event->data_len;
            memset(mqtt.data, '\0', sizeof(mqtt.data));
            memcpy(mqtt.data, event->data, event->data_len);
            xQueueSend(queue_sub_handle, &mqtt, 0);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(mqtt_tag, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                // log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                // log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                // log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(mqtt_tag, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
        default:
            ESP_LOGI(mqtt_tag, "Other event id:%d", event->event_id);
            break;
    }
}

esp_err_t ota_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(ota_tag, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(ota_tag, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(ota_tag, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(ota_tag, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(ota_tag, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(ota_tag, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(ota_tag, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}

esp_err_t version_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(ota_tag, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(ota_tag, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(ota_tag, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(ota_tag, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(ota_tag, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        if (!esp_http_client_is_chunked_response(evt->client)) 
        {
            memset(version_json_data, '\0', sizeof(version_json_data));
            strcpy(version_json_data, (char *)evt->data);
        }        
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(ota_tag, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(ota_tag, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;    
}

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if(wifi_retry < 60)
        {
            esp_wifi_connect();
            ESP_LOGI(wifi_tag, "Retry...");
            wifi_retry++;
        }
        else
        {
            ESP_LOGE(wifi_tag, "WiFi disconnected");
            // esp_wifi_stop();
            xEventGroupSetBits(event_group, SMART_CONFIG_START_BIT);            
        }
        gpio_set_level(WIFI_LED_NUM, 1);
        xEventGroupClearBits(event_group, WIFI_CONNECTED_BIT);
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
    {
        ESP_LOGI(wifi_tag, "WiFi connected"); 
    }
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        gpio_set_level(WIFI_LED_NUM, 0);
        xEventGroupSetBits(event_group, WIFI_CONNECTED_BIT);
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(wifi_tag, "WiFi connecting...");
        esp_wifi_connect();
    }
    else if(event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
    {
        ESP_LOGI(smart_config_wifi_tag, "Scan done");
    }
    else if(event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
    {
        ESP_LOGI(smart_config_wifi_tag, "Found channel");
    }
    else if(event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
    {
        ESP_LOGI(smart_config_wifi_tag, "Got SSID and password");
        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = {0};
        uint8_t password[65] = {0};
        uint8_t rvd_data[33] = {0};
        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) 
        {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }
        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(smart_config_wifi_tag, "SSID:%s", ssid);
        ESP_LOGI(smart_config_wifi_tag, "PASSWORD:%s", password);
        if(evt->type == SC_TYPE_ESPTOUCH_V2) 
        {
            ESP_ERROR_CHECK(esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)));
            ESP_LOGI(smart_config_wifi_tag, "RVD_DATA:");
            for(int i = 0; i < 33; i++) 
            {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }
        wifi_retry = 0;
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        esp_wifi_connect();
    }
    else if(event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
    {
        xEventGroupSetBits(event_group, SMART_CONFIG_DONE_BIT);
    }
}

static void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(ota_tag, "%s %s", label, hash_print);
}

void cut_string(char *source, char *des, char fir, char las)
{
    int i = 0, j = 0;
    bool start = false, end = false;
    for(i = 0; i < strlen(source); i++)
    {
        if(source[i] == fir)
        {
            start = true;
        }
        else if(source[i] == las)
        {
            end = true;
        }
        if(start == true)
        {
            des[j] = source[i];
            j++;
        }
        if(end == true)
        {
            break;
        }
    }
}

void ota_task(void)
{
    TickType_t pre_tick = 0;
    char des_version_json_data[256] = {0};
    xEventGroupWaitBits(event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(ota_tag, "Starting OTA task");
    esp_http_client_config_t version_config = {
        .url = "https://raw.githubusercontent.com/Vanperdung/ota_download/main/version.txt",
        .cert_pem = (char *)github_cert_pem_start,
        .event_handler = version_event_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&version_config);
    // esp_http_client_set_header(client, "Content-Type", "application/json");
    while(1)
    {
        xEventGroupWaitBits(event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        if((xTaskGetTickCount() - pre_tick) * portTICK_PERIOD_MS > 10000)
        {
            pre_tick = xTaskGetTickCount();
            ESP_LOGI(ota_tag, "Reading version...");
            esp_http_client_perform(client);
            esp_http_client_close(client);
            memset(des_version_json_data, '\0', sizeof(des_version_json_data));
            cut_string(version_json_data, des_version_json_data, '{', '}');
            printf("%s\n", des_version_json_data);
            cJSON *file_json = cJSON_Parse(des_version_json_data);
            if(file_json == NULL)
                ESP_LOGE(ota_tag, "File is invalid json");
            else
            {
                cJSON *link = cJSON_GetObjectItemCaseSensitive(file_json, "link");
                cJSON *version = cJSON_GetObjectItemCaseSensitive(file_json, "version");

                if(!cJSON_IsNumber(version))
                    ESP_LOGE(ota_tag, "Unable to read version");
                else
                {
                    double new_version = version->valuedouble;
                    if(new_version > FIRMWARE_VERSION)
                    {
                        ESP_LOGI(ota_tag, "New FIRMWARE version %.1f in GitHub", new_version);
                        ESP_LOGI(ota_tag, "Starting OTA FIRMWARE task");
                        esp_http_client_config_t ota_client_config = {
                            .url = link->valuestring,
                            .cert_pem = (char *)github_cert_pem_start,
                            .event_handler = ota_event_handler,
                            .keep_alive_enable = true,
                        };
                        esp_err_t ret = esp_https_ota(&ota_client_config);
                        if(ret == ESP_OK) 
                        {
                            ESP_LOGI(ota_tag, "OTA done, restarting...");
                            esp_restart();
                        } 
                        else 
                        {
                            ESP_LOGE(ota_tag, "OTA failed...");
                        }
                    }
                    else
                    {
                        ESP_LOGI(ota_tag, "Not new FIRMWARE");
                    }
                }
            }
        }
        else
        {
            vTaskDelay(100);
        }
    }
}

static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

void gpio_sta(void)
{
    gpio_reset_pin(RELAY_1_NUM);
    gpio_reset_pin(RELAY_2_NUM);
    gpio_reset_pin(RELAY_3_NUM);
    gpio_reset_pin(RELAY_4_NUM);
    gpio_reset_pin(WIFI_LED_NUM);

    gpio_set_direction(RELAY_1_NUM, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY_2_NUM, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY_3_NUM, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY_4_NUM, GPIO_MODE_OUTPUT);
    gpio_set_direction(WIFI_LED_NUM, GPIO_MODE_OUTPUT);

    gpio_set_level(RELAY_1_NUM, 1);
    gpio_set_level(RELAY_2_NUM, 1);
    gpio_set_level(RELAY_3_NUM, 1);
    gpio_set_level(RELAY_4_NUM, 1);
    gpio_set_level(WIFI_LED_NUM, 1);
}

void read_i2c_sensor_task(void)
{
    ESP_LOGI(i2c_sensor_tag, "Starting read I2C sensor task ");
    char aht10_temp[10] = {0};
    char aht10_hum[10] = {0};
    TickType_t pre_tick = 0;
    i2c_port_t i2c_port = 0;
    RTC_date_t current_date;
    RTC_time_t current_time;
    char *am_pm = NULL;
    char *cur_time = NULL;
    char *cur_date = NULL; 
    mqtt_struct_t mqtt_hum, mqtt_temp;
    // current_time.seconds = 30;
    // current_time.minutes = 17;
    // current_time.hours = 2;
    // current_time.time_format = TIME_FORMAT_24HRS;
    // current_date.day = WEDNESDAY;
    // current_date.date = 17;
    // current_date.month = 8;
    // current_date.year = 22;
    // ds1307_set_current_date(i2c_port, &current_date);
    // ds1307_set_current_time(i2c_port, &current_time);
    while(1)
    {
        if((xTaskGetTickCount() - pre_tick) * portTICK_PERIOD_MS > 5000)
        {
            read_data_aht10(i2c_port, aht10_temp, aht10_hum);
            ds1307_get_current_time(i2c_port, &current_time);
            ds1307_get_current_date(i2c_port, &current_date);
            pre_tick = xTaskGetTickCount();
            if(current_time.time_format != TIME_FORMAT_24HRS)
            {
                am_pm = (current_time.time_format) ? "PM" : "AM";
                cur_time = time_to_string(&current_time);
                cur_time[8] = ' ';
                cur_time[9] = am_pm[0];
                cur_time[10] = am_pm[1];
                cur_time[11] = '\0';
            }
            else
            {
	            cur_time = time_to_string(&current_time);
            }
            cur_date = date_to_string(&current_date);
            // ESP_LOGI(i2c_sensor_tag, "Humidity = %s, temperature = %s", aht10_hum, aht10_temp);
            // ESP_LOGI(i2c_sensor_tag, "%s %s", cur_time, cur_date);
            mqtt_hum.topic_type = PUBLISH;
            mqtt_hum.topic_len = strlen(HUMIDITY_TOPIC);
            memset(mqtt_hum.topic, '\0', sizeof(mqtt_hum.topic));
            memcpy(mqtt_hum.topic, HUMIDITY_TOPIC, mqtt_hum.topic_len);
            mqtt_hum.data_len = strlen(aht10_hum);
            memset(mqtt_hum.data, '\0', sizeof(mqtt_hum.data_len));
            memcpy(mqtt_hum.data, aht10_hum, mqtt_hum.data_len); 

            mqtt_temp.topic_type = PUBLISH;
            mqtt_temp.topic_len = strlen(TEMPERATURE_TOPIC);
            memset(mqtt_temp.topic, '\0', sizeof(mqtt_temp.topic));
            memcpy(mqtt_temp.topic, TEMPERATURE_TOPIC, mqtt_temp.topic_len);
            mqtt_temp.data_len = strlen(aht10_temp);
            memset(mqtt_temp.data, '\0', sizeof(mqtt_temp.data_len));
            memcpy(mqtt_temp.data, aht10_temp, mqtt_temp.data_len); 
            if(xEventGroupGetBits(event_group) & MQTT_CONNECTED_BIT)
            {
                xQueueSend(queue_sub_handle, &mqtt_hum, 0);
                xQueueSend(queue_sub_handle, &mqtt_temp, 0);
            }
        }
        else
        {
            vTaskDelay(100);
        }
    }
}

void smart_config_task(void)
{
    ESP_LOGI(smart_config_tag, "Starting smart config task");
    while(1)
    {
        xEventGroupWaitBits(event_group, SMART_CONFIG_START_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
        smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
        xEventGroupWaitBits(event_group, SMART_CONFIG_DONE_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        ESP_LOGI(smart_config_tag, "Smart config done");
        esp_smartconfig_stop();
        xEventGroupClearBits(event_group, SMART_CONFIG_START_BIT);
        xEventGroupClearBits(event_group, SMART_CONFIG_DONE_BIT);
    }
}

void mqtt_node_red_task(void)
{
    xEventGroupWaitBits(event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(mqtt_node_red_tag, "Starting MQTT node red task");
    mqtt_struct_t mqtt_buff;
    int relay_state;
    queue_sub_handle = xQueueCreate(15, sizeof(mqtt_struct_t));
    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://broker.hivemq.com",
        .keepalive = 60,
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler_cb, client);
    esp_mqtt_client_start(client);
    while(1)
    {
        xEventGroupWaitBits(event_group, MQTT_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY); 
        xQueueReceive(queue_sub_handle, &mqtt_buff, portMAX_DELAY);
        if(mqtt_buff.topic_type == SUBSCRIBE)
        {
            ESP_LOGI(mqtt_node_red_tag, "TOPIC = %.*s\r", mqtt_buff.topic_len, mqtt_buff.topic);
            ESP_LOGI(mqtt_node_red_tag, "DATA = %.*s\r", mqtt_buff.data_len, mqtt_buff.data);
            if(strstr(mqtt_buff.topic, RELAY_1_TOPIC) != NULL)
            {
                relay_state = (mqtt_buff.data[0] == '1') ? 0 : 1;
                gpio_set_level(RELAY_1_NUM, relay_state);
            }
            else if(strstr(mqtt_buff.topic, RELAY_2_TOPIC) != NULL)
            {
                relay_state = (mqtt_buff.data[0] == '1') ? 0 : 1;
                gpio_set_level(RELAY_2_NUM, relay_state);
            }
            else if(strstr(mqtt_buff.topic, RELAY_3_TOPIC) != NULL)
            {
                relay_state = (mqtt_buff.data[0] == '1') ? 0 : 1;
                gpio_set_level(RELAY_3_NUM, relay_state);
            }
            else if(strstr(mqtt_buff.topic, RELAY_4_TOPIC) != NULL)
            {
                relay_state = (mqtt_buff.data[0] == '1') ? 0 : 1;
                gpio_set_level(RELAY_4_NUM, relay_state);
            }
        }
        else if(mqtt_buff.topic_type == PUBLISH)
        {
            ESP_LOGI(mqtt_node_red_tag, "TOPIC = %.*s\r", mqtt_buff.topic_len, mqtt_buff.topic);
            ESP_LOGI(mqtt_node_red_tag, "DATA = %.*s\r", mqtt_buff.data_len, mqtt_buff.data);
            if(strstr(mqtt_buff.topic, HUMIDITY_TOPIC) != NULL)
            {
                esp_mqtt_client_publish(client, HUMIDITY_TOPIC, mqtt_buff.data, mqtt_buff.data_len, 0, true);
            }
            else if(strstr(mqtt_buff.topic, TEMPERATURE_TOPIC) != NULL)
            {
                esp_mqtt_client_publish(client, TEMPERATURE_TOPIC, mqtt_buff.data, mqtt_buff.data_len, 0, true);
            }
            else if(strstr(mqtt_buff.topic, POWER_TOPIC) != NULL)
            {
                esp_mqtt_client_publish(client, POWER_TOPIC, mqtt_buff.data, mqtt_buff.data_len, 0, true);
            }
        }
    }
}

void adc_power_task(void)
{
    ESP_LOGI(adc_power_tag, "Starting ADC power task");
    uint32_t u_adc, i_adc;
    uint32_t u_vol, i_vol;
    double u_real, i_real, p_real;
    esp_adc_cal_characteristics_t *adc_chars;
    TickType_t pre_tick = 0;
    mqtt_struct_t mqtt_power;
    char power[10]= {0};
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11);
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);;
    while(1)
    {
        if((xTaskGetTickCount() - pre_tick) * portTICK_PERIOD_MS > 1000)
        {
            pre_tick = xTaskGetTickCount();
            u_adc = 0;
            i_adc = 0;
            for (int i = 0; i < 20; i++) 
            {
                i_adc += adc1_get_raw(ADC1_CHANNEL_4);
                u_adc += adc1_get_raw(ADC1_CHANNEL_5);
            }
            i_adc /= 20;
            u_adc /= 20;
            u_vol = esp_adc_cal_raw_to_voltage(u_adc, adc_chars);
            i_vol = esp_adc_cal_raw_to_voltage(i_adc, adc_chars);
            u_real = (double)u_vol * (13.3 / 3.3) / 1000.0;
            i_real = ((double)i_vol - 2650.0) / 0.185;
            if(i_real <= 0)
                i_real = 0;
            p_real = u_real * i_real / 1000.0;
            ESP_LOGI(adc_power_tag, "P = %f, I = %f, U = %f", p_real, i_real / 1000, u_real);
            ftoa((float)p_real, power, 2);
            mqtt_power.topic_type = PUBLISH;
            mqtt_power.topic_len = strlen(POWER_TOPIC);
            memset(mqtt_power.topic, '\0', sizeof(mqtt_power.topic));
            memcpy(mqtt_power.topic, POWER_TOPIC, mqtt_power.topic_len);
            mqtt_power.data_len = strlen(power);
            memset(mqtt_power.data, '\0', sizeof(mqtt_power.data_len));
            memcpy(mqtt_power.data, power, mqtt_power.data_len);
            if(xEventGroupGetBits(event_group) & MQTT_CONNECTED_BIT)
            {
                xQueueSend(queue_sub_handle, &mqtt_power, 0);
            }
        }
        else
        {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
}

void i2c_master_initialize(i2c_port_t i2c_num)
{
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_I2C_SDA,
        .scl_io_num = GPIO_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_CLOCK_FREQ
    };
    i2c_param_config(i2c_num, &i2c_config);
    i2c_driver_install(i2c_num, i2c_config.mode, 0, 0, 0);
}

void app_main(void)
{
    i2c_port_t i2c_port = 0;
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    get_sha256_of_partitions();
    gpio_sta();
    i2c_master_initialize(i2c_port);
    // esp_get_free_heap_size();
    // esp_get_minimum_free_heap_size();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_config);
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = "Nhung Toan",
            .password = "99999999"
        }
    };
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_sta_config);
    esp_wifi_start();
    event_group = xEventGroupCreate();
    xTaskCreate(ota_task, "ota_task", 8192, NULL, 7, &ota_task_handle);
    xTaskCreate(read_i2c_sensor_task, "read_i2c_sensor_task", 2048, NULL, 5, &read_i2c_sensor_task_handle);
    xTaskCreate(smart_config_task, "smart_config_task", 4096, NULL, 8, &smart_config_task_handle);
    xTaskCreate(mqtt_node_red_task, "mqtt_node_red_task", 8192, NULL, 4, &mqtt_adafruit_task_handle);
    xTaskCreate(adc_power_task, "adc_power_task", 2048, NULL, 6, &adc_power_task_handle);
}
