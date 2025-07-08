#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "mqtt_client.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define ESP_WIFI_SSID      "LiB"
#define ESP_WIFI_PASS      "123456789"
#define ESP_MAXIMUM_RETRY  5

#define LED_PIN             2

#define MQTT_Broker "mqtt://demo.thingsboard.io:1883"
#define USER_NAME   "KOB9rV5CSP0GXJsYsDya"

#define RPC_REQUEST_TOPIC   "v1/devices/me/rpc/request/+"
#define TELEMETRY_TOPIC     "v1/devices/me/telemetry"
#define ATTRIBUTES_TOPIC    "v1/devices/me/attributes"

static const char *TAG = "LED_TB";

static int s_retry_num = 0;
esp_mqtt_client_handle_t client;
int led = 0;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){
    // char *data = NULL;
    ESP_LOGI(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    // esp_mqtt_client_handle_t client = event->client;
    // int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_mqtt_client_subscribe(client, RPC_REQUEST_TOPIC, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break; 
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;   
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        //esp_mqtt_client_subscribe(client,"messages/0931b3cc-bdd4-4bc9-a2b1-a18eb1e15ff7/status", 0);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        if(strstr(event->topic, "request") != NULL){
            if(strstr(event->data, "ON") != NULL){
                led = 1;
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");

        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void){
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_Broker,
        .credentials.username = USER_NAME,
        .session.keepalive = 60,
        .session.disable_clean_session = false,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            ESP_LOGI(TAG,"connect to the AP fail");   
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        mqtt_app_start();
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "LiB",
            .password = "123456789",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .sae_h2e_identifier = "",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

void ledTask(void *pvParameters){
    while (1)
    {
        if(led){
            char data[256] = { 0 };
            strcpy(data, "{ \"status\" : \"ON\" }");
            gpio_set_level(LED_PIN, 1);
            esp_mqtt_client_publish(client, TELEMETRY_TOPIC, data, strlen(data), 1, false);
            esp_mqtt_client_publish(client, ATTRIBUTES_TOPIC, data, strlen(data), 1, false);
            led = 0;
            vTaskDelay(2000/portTICK_PERIOD_MS);
            
            memset(data, 0, sizeof(data));
            strcpy(data, "{ \"status\" : \"OFF\" }");
            gpio_set_level(LED_PIN, 0);
            esp_mqtt_client_publish(client, TELEMETRY_TOPIC, data, strlen(data), 1, false);
            esp_mqtt_client_publish(client, ATTRIBUTES_TOPIC, data, strlen(data), 1, false);
        }
        vTaskDelay(10/portTICK_PERIOD_MS);
    }
    
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    xTaskCreate(ledTask, "LED Task", 4096, NULL, 5, NULL);
}
