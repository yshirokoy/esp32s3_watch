#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>

typedef struct {
    float temperature;      // 温度 °C
    int   weather_code;     // WMO weather code
    int   humidity;         // 湿度 %
    float wind_speed;       // 风速 km/h
    bool  valid;            // 数据是否有效
} weather_data_t;

const weather_data_t *weather_get_current(void);
const char          *weather_get_update_time(void);
const char          *weather_code_to_text(int code);
void                 weather_init(void);
void                 weather_fetch_now(void);
void                 weather_request_refresh(void);

#endif
