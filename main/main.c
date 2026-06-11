#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "tftlcd.h"
#include "cst816t.h"
#include "watch_ui.h"
#include "wifi_mgr.h"
#include "weather.h"

#define LVGL_TICK_MS   2
#define LVGL_DRAW_ROWS LCD_BUF_ROWS

static const char *TAG = "watch_main";

// --- LVGL display flush callback ---
static void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                          lv_color_t *color_p)
{
    lcd_draw_bitmap(area->x1, area->y1, area->x2, area->y2,
                    (const uint16_t *)color_p);
    lv_disp_flush_ready(disp_drv);
}

// --- LVGL touch indev callback (reads from shared CST816T state) ---
static void my_touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    (void)indev_drv;
    uint16_t x = 0, y = 0;

    if (cst816t_get_latest(&x, &y)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// --- WiFi init runs in its own task: crash-safe for LVGL ---
static void wifi_init_task(void *arg)
{
    (void)arg;

    // Small delay to let LVGL start first
    vTaskDelay(pdMS_TO_TICKS(500));

    wifi_mgr_init();

    // If we get here, WiFi init completed (success or failure logged)
    ESP_LOGI(TAG, "WiFi init task done");
    vTaskDelete(NULL);
}

// --- Main ---
void app_main(void)
{
    // 1. Display init
    lcd_init();
    lcd_fill_color(BLACK);

    // 2. LVGL core init
    lv_init();

    const size_t buffer_pixels = LCD_WIDTH * LVGL_DRAW_ROWS;
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
        buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
        buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);

    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        if (buf1) heap_caps_free(buf1);
        if (buf2) heap_caps_free(buf2);
        return;
    }

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buffer_pixels);

    // 3. Register display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // 4. Touch init + register indev
    if (cst816t_init() == ESP_OK) {
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = my_touch_read_cb;
        lv_indev_drv_register(&indev_drv);

        cst816t_task_start();  // Start touch polling task
    }

    // 5. Create watch UI on screen (before WiFi, so UI is visible immediately)
    watch_ui_create();

    // 6. Start WiFi in its own task (crash-safe for LVGL)
    // WiFi init involves deep call stacks (NVS, netif, event loop, TCP/IP) — need 8KB+
    xTaskCreate(wifi_init_task, "wifi_init", 8192, NULL, 3, NULL);

    // 7. Start weather fetch task
    weather_init();

    ESP_LOGI(TAG, "Init done, entering LVGL loop");

    // 8. Main task runs LVGL loop with accurate timing
    int64_t last_tick_us = esp_timer_get_time();
    while (1) {
        int64_t now_us = esp_timer_get_time();
        uint32_t elapsed_ms = (uint32_t)((now_us - last_tick_us) / 1000);
        if (elapsed_ms > 0) {
            lv_tick_inc(elapsed_ms);
            last_tick_us = now_us;
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
