#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdlib.h>
#include "tftlcd.h"
// --- 引脚定义 ---
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   9
#define TFT_RST  8
#define TFT_BLK  18

#define LCD_HOST SPI2_HOST

static const char *TAG = "TFTLCD";

spi_device_handle_t spi;

// --- DMA异步刷新事务池 (双缓冲安全，最多2个并发) ---
#define LCD_FLUSH_POOL_SIZE 4
static spi_transaction_t s_flush_pool[LCD_FLUSH_POOL_SIZE];
static int s_flush_pool_idx = 0;

// ================== GPIO初始化 ==================
static void lcd_gpio_init(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TFT_DC) |
                        (1ULL << TFT_RST) |
                        (1ULL << TFT_BLK),
    };
    gpio_config(&io_conf);
}

// ================== SPI初始化 ==================
static void lcd_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = TFT_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = TFT_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_BUF_ROWS * sizeof(uint16_t),
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 60 * 1000 * 1000, // 60MHz (ST7789 max ~100MHz)
        .mode = 0,
        .spics_io_num = TFT_CS,
        .queue_size = 7,
    };

    spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(LCD_HOST, &devcfg, &spi);
}

// ================== 复位 + 背光 ==================
static void lcd_reset(void)
{
    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void lcd_backlight_on(void)
{
    gpio_set_level(TFT_BLK, 1);
}

// ================== 命令/数据发送 ==================
static void lcd_send_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    gpio_set_level(TFT_DC, 0); // 命令
    spi_device_polling_transmit(spi, &t);
}

static void lcd_send_data(uint8_t data)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
    };
    gpio_set_level(TFT_DC, 1); // 数据
    spi_device_polling_transmit(spi, &t);
}

// ================== ST7789S初始化 ==================
static void lcd_init_cmds(void)
{
    lcd_send_cmd(0x36);
    lcd_send_data(0x08);
    lcd_send_data(0x00);  // 方向（可调）

    lcd_send_cmd(0x3A);
    lcd_send_data(0x55);  // RGB565

    lcd_send_cmd(0x21);   // 反色（有些屏需要）

    lcd_send_cmd(0x11);   // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_send_cmd(0x29);   // Display ON
}

// ================== 总初始化函数 ==================
void lcd_init(void)
{
    lcd_gpio_init();
    lcd_spi_init();

    lcd_reset();
    lcd_backlight_on();

    lcd_init_cmds();
}


// Change these if the visible image is shifted on the panel.
#define X_OFFSET 0
#define Y_OFFSET 20

void st7789_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    x1 += X_OFFSET;
    x2 += X_OFFSET;
    y1 += Y_OFFSET;
    y2 += Y_OFFSET;

    // 列地址设置
    lcd_send_cmd(0x2A);
    lcd_send_data(x1 >> 8);
    lcd_send_data(x1 & 0xFF);
    lcd_send_data(x2 >> 8);
    lcd_send_data(x2 & 0xFF);

    // 行地址设置
    lcd_send_cmd(0x2B);
    lcd_send_data(y1 >> 8);
    lcd_send_data(y1 & 0xFF);
    lcd_send_data(y2 >> 8);
    lcd_send_data(y2 & 0xFF);

    // 写GRAM
    lcd_send_cmd(0x2C);
}

void lcd_fill_color(uint16_t color)
{
    uint16_t buf[LCD_WIDTH];
    
    // 设置全屏窗口
    st7789_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    for (int y = 0; y < LCD_HEIGHT; y++)
    {
        for (int i = 0; i < LCD_WIDTH; i++)
            buf[i] = color;

        spi_transaction_t t = {
            .length = sizeof(buf) * 8,
            .tx_buffer = buf,
        };

        gpio_set_level(TFT_DC, 1);
        spi_device_polling_transmit(spi, &t);
    }
}

// ================== 画点 ==================
void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    st7789_set_window(x, y, x, y);

    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = &color,
    };

    gpio_set_level(TFT_DC, 1);
    spi_device_polling_transmit(spi, &t);
}

// ================== 水平线 ==================
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    if (y >= LCD_HEIGHT) return;
    if (x + len > LCD_WIDTH) len = LCD_WIDTH - x;

    uint16_t buf[LCD_WIDTH];

    for (int i = 0; i < len; i++)
        buf[i] = color;

    st7789_set_window(x, y, x + len - 1, y);

    spi_transaction_t t = {
        .length = len * 16,
        .tx_buffer = buf,
    };

    gpio_set_level(TFT_DC, 1);
    spi_device_polling_transmit(spi, &t);
}

// ================== 垂直线 ==================
void lcd_draw_vline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    for (int i = 0; i < len; i++)
        lcd_draw_pixel(x, y + i, color);
}

// ================== 画线 ==================
void lcd_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1)
    {
        lcd_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;

        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ================== 矩形 ==================
void lcd_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    lcd_draw_hline(x, y, w, color);
    lcd_draw_hline(x, y + h - 1, w, color);
    lcd_draw_vline(x, y, h, color);
    lcd_draw_vline(x + w - 1, y, h, color);
}

// ================== 填充矩形 ==================
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;

    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;

    st7789_set_window(x, y, x + w - 1, y + h - 1);

    uint16_t buf[LCD_WIDTH];

    for (int i = 0; i < w; i++)
        buf[i] = color;

    for (int j = 0; j < h; j++)
    {
        spi_transaction_t t = {
            .length = w * 16,
            .tx_buffer = buf,
        };

        gpio_set_level(TFT_DC, 1);
        spi_device_polling_transmit(spi, &t);
    }
}

// ================== 字符 ==================
void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg)
{
    if (c < 32 || c > 127) return;

    const uint8_t *bitmap = font5x8[c - 32];

    for (int i = 0; i < 5; i++)
    {
        uint8_t line = bitmap[i];
        for (int j = 0; j < 8; j++)
        {
            if (line & 0x01)
                lcd_draw_pixel(x + i, y + j, color);
            else
                lcd_draw_pixel(x + i, y + j, bg);

            line >>= 1;
        }
    }
}

// ================== 字符串 ==================
void lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg)
{
    while (*str)
    {
        lcd_draw_char(x, y, *str, color, bg);
        x += 6;
        str++;
    }
}

// ================== 图片 ==================
void lcd_draw_image(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *img)
{
    st7789_set_window(x, y, x + w - 1, y + h - 1);

    spi_transaction_t t = {
        .length = w * h * 16,
        .tx_buffer = img,
    };

    gpio_set_level(TFT_DC, 1);
    spi_device_polling_transmit(spi, &t);
}

// ================== LVGL接口 (同步版 - 阻塞式) ==================
void lcd_draw_bitmap(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, const uint16_t *bitmap)
{
    uint16_t w = x1 - x0 + 1;
    uint16_t h = y1 - y0 + 1;

    st7789_set_window(x0, y0, x1, y1);

    spi_transaction_t t = {
        .length = w * h * 16,
        .tx_buffer = bitmap,
    };

    gpio_set_level(TFT_DC, 1);
    spi_device_polling_transmit(spi, &t);
}

// ================== LVGL异步刷新 (DMA队列传输 - CPU不等待) ==================
// 调用后立即返回，DMA在后台传输数据。传输完成后须由
// 另一个任务调用 spi_device_get_trans_result() 获取结果并通知LVGL。
void lcd_flush_async(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, const uint16_t *bitmap)
{
    uint16_t w = x1 - x0 + 1;
    uint16_t h = y1 - y0 + 1;

    st7789_set_window(x0, y0, x1, y1);

    // 从事务池获取下一个空闲事务 (环形缓冲，LVGL双缓冲最多2个并发)
    spi_transaction_t *t = &s_flush_pool[s_flush_pool_idx];
    s_flush_pool_idx = (s_flush_pool_idx + 1) % LCD_FLUSH_POOL_SIZE;

    memset(t, 0, sizeof(*t));
    t->length = w * h * 16;
    t->tx_buffer = bitmap;

    gpio_set_level(TFT_DC, 1);
    spi_device_queue_trans(spi, t, portMAX_DELAY);
}

// 等待DMA传输完成 (供main.c的flush_task调用)
void lcd_flush_wait(void)
{
    spi_transaction_t *ret_trans;
    spi_device_get_trans_result(spi, &ret_trans, portMAX_DELAY);
}
