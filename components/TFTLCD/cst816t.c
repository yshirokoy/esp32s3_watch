#include "driver/i2c.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <stdbool.h>
#include "cst816t.h"
#include "tftlcd.h"

#define CST816T_I2C_PORT       I2C_NUM_0
#define CST816T_I2C_ADDR       0x15
#define CST816T_I2C_FREQ_HZ    400000
#define CST816T_I2C_TIMEOUT_MS 2
#define CST816T_POLL_INTERVAL_MS 10

#define CST816T_REG_GESTURE_ID 0x01
#define CST816T_REG_FINGER_NUM 0x02
#define CST816T_REG_XPOS_H     0x03
#define CST816T_REG_XPOS_L     0x04
#define CST816T_REG_YPOS_H     0x05
#define CST816T_REG_YPOS_L     0x06
#define CST816T_REG_CHIP_ID    0xA7

static const char *TAG = "CST816T";

static bool s_i2c_ready = false;

// --- Shared touch state (protected by mutex) ---
static SemaphoreHandle_t s_mutex = NULL;
static uint16_t s_shared_x = LCD_WIDTH / 2;
static uint16_t s_shared_y = LCD_HEIGHT / 2;
static bool     s_shared_pressed = false;

static esp_err_t cst816t_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(CST816T_I2C_PORT,
                                        CST816T_I2C_ADDR,
                                        &reg,
                                        1,
                                        data,
                                        len,
                                        pdMS_TO_TICKS(CST816T_I2C_TIMEOUT_MS));
}

static esp_err_t cst816t_i2c_init(void)
{
    if (s_i2c_ready) {
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CST816T_SDA_GPIO,
        .scl_io_num = CST816T_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CST816T_I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_param_config(CST816T_I2C_PORT, &conf);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_driver_install(CST816T_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = ESP_OK;
    }

    s_i2c_ready = (ret == ESP_OK);
    return ret;
}

static void cst816t_gpio_init(void)
{
    gpio_config_t rst_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << CST816T_RST_GPIO),
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_conf);

    gpio_config_t int_conf = {
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << CST816T_INT_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_conf);
}

static void cst816t_reset(void)
{
    gpio_set_level(CST816T_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(CST816T_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static bool cst816t_wait_ready(void)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(500);
    while (xTaskGetTickCount() < deadline) {
        if (gpio_get_level(CST816T_INT_GPIO) == 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

static void cst816t_transform_point(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y)
{
    int tx = raw_x;
    int ty = raw_y;

#if CST816T_SWAP_XY
    int tmp = tx;
    tx = ty;
    ty = tmp;
#endif

    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx >= LCD_WIDTH) tx = LCD_WIDTH - 1;
    if (ty >= LCD_HEIGHT) ty = LCD_HEIGHT - 1;

#if CST816T_MIRROR_X
    tx = (LCD_WIDTH - 1) - tx;
#endif

#if CST816T_MIRROR_Y
    ty = (LCD_HEIGHT - 1) - ty;
#endif

    *x = (uint16_t)tx;
    *y = (uint16_t)ty;
}

// ==================== Touch polling task ====================
static void cst816t_task(void *arg)
{
    (void)arg;
    uint16_t last_x = s_shared_x;
    uint16_t last_y = s_shared_y;
    uint8_t  last_gesture = 0;

    ESP_LOGI(TAG, "Touch task started (poll interval: %d ms)", CST816T_POLL_INTERVAL_MS);

    while (1) {
        uint16_t x = 0, y = 0;
        uint8_t gesture = 0;
        bool pressed;

        if (cst816t_read_touch(&x, &y, &gesture)) {
            pressed = true;
            last_x = x;
            last_y = y;
        } else {
            pressed = false;
            // keep last known position
        }

        // Publish to shared state
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            s_shared_x = last_x;
            s_shared_y = last_y;
            s_shared_pressed = pressed;
            xSemaphoreGive(s_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(CST816T_POLL_INTERVAL_MS));
    }
}

// ==================== Thread-safe public API ====================
bool cst816t_get_latest(uint16_t *x, uint16_t *y)
{
    bool pressed;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        *x = s_shared_x;
        *y = s_shared_y;
        pressed = s_shared_pressed;
        xSemaphoreGive(s_mutex);
    } else {
        pressed = false;
    }
    return pressed;
}

// ==================== Init ====================
esp_err_t cst816t_init(void)
{
    s_mutex = xSemaphoreCreateMutex();

    cst816t_gpio_init();
    cst816t_reset();

    if (!cst816t_wait_ready()) {
        ESP_LOGW(TAG, "CST816T INT pin did not go low — trying I2C anyway");
    }

    esp_err_t ret = cst816t_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CST816T I2C init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t chip_id = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        ret = cst816t_read_regs(CST816T_REG_CHIP_ID, &chip_id, 1);
        if (ret == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CST816T ready, chip id: 0x%02X", chip_id);
    } else {
        ESP_LOGW(TAG, "CST816T did not respond after 3 retries: %s", esp_err_to_name(ret));
    }

    return ret;
}

void cst816t_task_start(void)
{
    xTaskCreate(cst816t_task, "cst816t", 3072, NULL, 4, NULL);
}

// ==================== Low-level read (used by polling task) ====================
bool cst816t_read_touch(uint16_t *x, uint16_t *y, uint8_t *gesture)
{
    uint8_t data[6] = {0};
    esp_err_t ret = cst816t_read_regs(CST816T_REG_GESTURE_ID, data, sizeof(data));
    if (ret != ESP_OK) {
        return false;
    }

    uint8_t finger_num = data[CST816T_REG_FINGER_NUM - CST816T_REG_GESTURE_ID] & 0x0F;

    if (gesture != NULL) {
        *gesture = data[0];
    }

    if (finger_num == 0 || finger_num > 1) {
        return false;
    }

    uint16_t raw_x = ((uint16_t)(data[CST816T_REG_XPOS_H - CST816T_REG_GESTURE_ID] & 0x0F) << 8) |
                     data[CST816T_REG_XPOS_L - CST816T_REG_GESTURE_ID];
    uint16_t raw_y = ((uint16_t)(data[CST816T_REG_YPOS_H - CST816T_REG_GESTURE_ID] & 0x0F) << 8) |
                     data[CST816T_REG_YPOS_L - CST816T_REG_GESTURE_ID];

    cst816t_transform_point(raw_x, raw_y, x, y);
    return true;
}

// ==================== Slide demo (kept for compatibility) ====================
#include "lvgl.h"

void cst816t_create_slide_demo(void)
{
    lv_obj_clean(lv_scr_act());

    lv_obj_t *tileview = lv_tileview_create(lv_scr_act());
    lv_obj_set_size(tileview, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *t1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *t2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *t3 = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_LEFT);

    uint32_t colors[] = {0x1976D2, 0x00897B, 0x7B1FA2};
    const char *titles[] = {"CST816T", "Touch OK", "Slide Demo"};
    const char *hints[] = {"Swipe left", "Swipe left or right", "Swipe right"};
    lv_obj_t *tiles[] = {t1, t2, t3};

    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(tiles[i], lv_color_hex(colors[i]), 0);
        lv_obj_set_style_bg_opa(tiles[i], LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(tiles[i], 18, 0);

        lv_obj_t *label = lv_label_create(tiles[i]);
        lv_label_set_text(label, titles[i]);
        lv_obj_set_width(label, LCD_WIDTH - 36);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, -18);

        lv_obj_t *hint = lv_label_create(tiles[i]);
        lv_label_set_text(hint, hints[i]);
        lv_obj_set_width(hint, LCD_WIDTH - 36);
        lv_obj_set_style_text_color(hint, lv_color_white(), 0);
        lv_obj_set_style_text_opa(hint, LV_OPA_70, 0);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 18);
    }

    lv_obj_set_tile_id(tileview, 0, 0, LV_ANIM_OFF);
}
