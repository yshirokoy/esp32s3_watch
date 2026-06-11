set(src_dirs
                TFTLCD
                WATCH_UI
                WIFI_MGR
                WEATHER
                game)
set(include_dirs
                TFTLCD
                WATCH_UI
                WIFI_MGR
                WEATHER
                game)
set(requires
            driver
            lvgl__lvgl
            esp_wifi
            nvs_flash
            esp_http_client
            json)


idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
