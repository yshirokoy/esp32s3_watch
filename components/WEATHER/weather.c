#include "weather.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "wifi_mgr.h"

#define WEATHER_UPDATE_INTERVAL_MS (30 * 60 * 1000) // 30 minutes
#define WEATHER_HTTP_TIMEOUT_MS    10000
#define WEATHER_URL \
    "http://api.open-meteo.com/v1/forecast?" \
    "latitude=31.30&longitude=104.51" \
    "&current=temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m" \
    "&timezone=auto"

static const char *TAG = "WEATHER";
static weather_data_t s_weather = {0};
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_task_handle = NULL;
static char s_update_time[6] = "--:--";

// WMO weather code → English description
static const char *s_code_text[100] = {
    [0]  = "Clear",
    [1]  = "P. Cloudy", [2] = "Cloudy",   [3] = "Overcast",
    [45] = "Fog",       [46] = "Rime Fog", [47] = "Dense Fog", [48] = "Rime Fog",
    [51] = "Lt Driz",   [53] = "Drizzle",  [55] = "Hvy Driz",
    [56] = "Frz Driz",  [57] = "Frz Driz",
    [61] = "Lt Rain",   [63] = "Rain",     [65] = "Hvy Rain",
    [66] = "Frz Rain",  [67] = "Frz Rain",
    [71] = "Lt Snow",   [73] = "Snow",     [75] = "Hvy Snow",
    [77] = "Graupel",
    [80] = "Showers",   [81] = "Showers",  [82] = "Hvy Showers",
    [85] = "Snw Show",  [86] = "Snw Show",
    [95] = "T-Storm",   [96] = "Hail",     [97] = "Hail",
    [98] = "T-Storm",   [99] = "T-Storm",
};

const char *weather_code_to_text(int code)
{
    if (code >= 0 && code < 100 && s_code_text[code] != NULL) {
        return s_code_text[code];
    }
    return "N/A";
}

const weather_data_t *weather_get_current(void)
{
    return &s_weather;
}

const char *weather_get_update_time(void)
{
    return s_update_time;
}

static int weather_http_handler(esp_http_client_event_t *evt)
{
    static char *rx_buf = NULL;
    static int   rx_len = 0;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP connected");
        break;
    case HTTP_EVENT_ON_HEADER:
        break;
    case HTTP_EVENT_ON_DATA:
        if (evt->data_len > 0) {
            char *new_buf = realloc(rx_buf, rx_len + evt->data_len + 1);
            if (new_buf == NULL) {
                free(rx_buf);
                rx_buf = NULL;
                rx_len = 0;
                break;
            }
            rx_buf = new_buf;
            memcpy(rx_buf + rx_len, evt->data, evt->data_len);
            rx_len += evt->data_len;
            rx_buf[rx_len] = '\0';
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        if (rx_buf != NULL) {
            ESP_LOGI(TAG, "HTTP response: %s", rx_buf);

            cJSON *root = cJSON_Parse(rx_buf);
            if (root != NULL) {
                cJSON *current = cJSON_GetObjectItem(root, "current");
                if (current != NULL) {
                    xSemaphoreTake(s_mutex, portMAX_DELAY);

                    cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
                    cJSON *code = cJSON_GetObjectItem(current, "weather_code");
                    cJSON *humi = cJSON_GetObjectItem(current, "relative_humidity_2m");
                    cJSON *wind = cJSON_GetObjectItem(current, "wind_speed_10m");

                    if (temp != NULL) s_weather.temperature = (float)temp->valuedouble;
                    if (code != NULL) s_weather.weather_code = code->valueint;
                    if (humi != NULL) s_weather.humidity = humi->valueint;
                    if (wind != NULL) s_weather.wind_speed = (float)wind->valuedouble;
                    s_weather.valid = true;

                    // Record update time
                    time_t now;
                    time(&now);
                    struct tm *tm_info = localtime(&now);
                    snprintf(s_update_time, sizeof(s_update_time),
                             "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);

                    xSemaphoreGive(s_mutex);

                    ESP_LOGI(TAG, "Weather updated: %.1fC, %s, %d%%, %.1fkm/h",
                             s_weather.temperature,
                             weather_code_to_text(s_weather.weather_code),
                             s_weather.humidity,
                             s_weather.wind_speed);
                }
                cJSON_Delete(root);
            }
            free(rx_buf);
            rx_buf = NULL;
            rx_len = 0;
        }
        break;
    case HTTP_EVENT_DISCONNECTED:
        free(rx_buf);
        rx_buf = NULL;
        rx_len = 0;
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void weather_task(void *arg)
{
    (void)arg;
    s_task_handle = xTaskGetCurrentTaskHandle();

    // Wait for WiFi to connect first
    ESP_LOGI(TAG, "Waiting for WiFi...");
    while (!wifi_mgr_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TAG, "WiFi ready, starting weather fetch");

    while (1) {
        weather_fetch_now();
        // Wait 30 min or until manual refresh request
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WEATHER_UPDATE_INTERVAL_MS));
    }
}

// Non-blocking: signals the weather task to fetch immediately
void weather_request_refresh(void)
{
    if (s_task_handle) {
        xTaskNotifyGive(s_task_handle);
    }
}

void weather_fetch_now(void)
{
    esp_http_client_config_t config = {
        .url = WEATHER_URL,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .event_handler = weather_http_handler,
        .buffer_size = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d", status);
    } else {
        ESP_LOGW(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void weather_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreate(weather_task, "weather", 8192, NULL, 5, NULL);
}
