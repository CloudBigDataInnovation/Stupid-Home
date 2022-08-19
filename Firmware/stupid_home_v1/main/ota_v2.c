
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"
#include "driver/gpio.h"
#include "string.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_tls.h"
#include "cJSON.h"

#define HASH_LEN            32
#define OTA_URL_SIZE        256
#define WIFI_CONNECTED_BIT  BIT0
#define FIRMWARE_VERSION    0.2

static const char *ota_tag = "OTA TASK";
extern const uint8_t github_cert_pem_start[] asm("_binary_git_ota_pem_start");
extern const uint8_t github_cert_pem_end[] asm("_binary_git_ota_pem_end");

TaskHandle_t ota_task_handle;
char version_json_data[512] = {0};
EventGroupHandle_t xCreatedEventGroup;

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

esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id)
    {
        case SYSTEM_EVENT_STA_DISCONNECTED:
            printf("Retry...\n");
            esp_wifi_connect();
            gpio_set_level(GPIO_NUM_15, 1);
            xEventGroupClearBits(xCreatedEventGroup, WIFI_CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            printf("WiFi connected\n");
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            printf("IP: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.ip));
            gpio_set_level(GPIO_NUM_15, 0);
            xEventGroupSetBits(xCreatedEventGroup, WIFI_CONNECTED_BIT);
            break;
        case SYSTEM_EVENT_STA_START:
            printf("WiFi connecting...\n");
            esp_wifi_connect();
            break;
        default:
            break;
    }
    return ESP_OK;
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
    xEventGroupWaitBits(xCreatedEventGroup, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(ota_tag, "Starting read version task");
    esp_http_client_config_t version_config = {
        .url = "https://raw.githubusercontent.com/Vanperdung/ota_download/main/version.txt",
        .cert_pem = (char *)github_cert_pem_start,
        .event_handler = version_event_handler
    };
    esp_http_client_handle_t client = esp_http_client_init(&version_config);
    // esp_http_client_set_header(client, "Content-Type", "application/json");
    while(1)
    {
        xEventGroupWaitBits(xCreatedEventGroup, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
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
    gpio_reset_pin(GPIO_NUM_0);
    gpio_reset_pin(GPIO_NUM_4);
    gpio_reset_pin(GPIO_NUM_15);
    gpio_reset_pin(GPIO_NUM_16);
    gpio_reset_pin(GPIO_NUM_17);

    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_15, GPIO_MODE_OUTPUT);

    gpio_set_level(GPIO_NUM_0, 1);
    gpio_set_level(GPIO_NUM_4, 1);
    gpio_set_level(GPIO_NUM_16, 1);
    gpio_set_level(GPIO_NUM_15, 1);
    gpio_set_level(GPIO_NUM_17, 1);
}

void app_main(void)
{
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
    // esp_get_free_heap_size();
    // esp_get_minimum_free_heap_size();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_event_loop_init(wifi_event_handler, NULL);
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
    xCreatedEventGroup = xEventGroupCreate();
    xTaskCreate(&ota_task, "ota_task", 8192, NULL, 6, &ota_task_handle);
}
