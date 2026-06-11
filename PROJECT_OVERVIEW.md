# 🖥️ ESP32-S3 Smart Watch — 项目总览文档

> 最后更新: 2026-06-11

---

## 一、项目结构

```
├── main/
│   ├── CMakeLists.txt          # 主组件：REQUIRES components
│   └── main.c                  # 入口：LVGL初始化、双缓冲、主循环
│
├── components/
│   ├── CMakeLists.txt          # 子组件注册：6个子目录
│   │
│   ├── TFTLCD/                 # ST7789S 显示屏驱动
│   │   ├── tftlcd.h            # 240×280 定义、颜色宏、字体库、API声明
│   │   ├── tftlcd.c            # SPI 60MHz、画点/线/矩形/字符串、LVGL flush
│   │   ├── cst816t.h           # CST816T 触摸芯片 GPIO 定义、API声明
│   │   └── cst816t.c           # I2C 触摸驱动、10ms轮询、坐标变换
│   │
│   ├── WATCH_UI/               # 手表 UI（4页TileView）
│   │   ├── watch_ui.h          # API声明
│   │   └── watch_ui.c          # 4页布局 + 计时器 + 游戏锁 + 每秒tick
│   │
│   ├── WIFI_MGR/               # WiFi + NTP 时间同步
│   │   ├── wifi_mgr.h          # API声明
│   │   └── wifi_mgr.c          # STA模式连接、SNTP(NTP)时间同步、事件处理
│   │
│   ├── WEATHER/                # 天气数据获取
│   │   ├── weather.h           # 数据结构、API声明
│   │   └── weather.c           # HTTP请求open-meteo.com、JSON解析、任务通知刷新
│   │
│   └── game/                   # 2048 小游戏
│       ├── game_2048.h         # API声明
│       └── game_2048.c         # 4×4棋盘、滑动逻辑、分数、TileView锁定
│
├── partitions-16Mib.csv        # 自定义分区表 (factory ~1.94MB)
├── sdkconfig                   # ESP-IDF 配置
└── managed_components/
    └── lvgl__lvgl/             # LVGL 8.x 图形库
```

---

## 二、4页界面总览

| 页码 | 名称 | 功能 | 配色 | 关键文件 |
|------|------|------|------|----------|
| 0 | 🏠 表盘 | 时间、日期、步数、心率、天气摘要、弧形进度 | 暖色 (espresso `#1C110A`) | watch_ui.c `build_watch_face()` |
| 1 | 🌤️ 天气 | 太阳/月亮图标、温度、描述、湿度、风速、刷新按钮 | 蓝白 (`#EAF0F6`) | watch_ui.c `build_weather_page()` |
| 2 | ⏱️ 计时器 | 00:00.00 秒表、Start/Stop、Reset | 暖奶油 (`#F5F0EB`) | watch_ui.c `build_timer_page()` |
| 3 | 🎮 2048 | 经典数字合并游戏、触屏滑动操控、分数统计 | 经典棋盘配色 | game/game_2048.c |

### 页面导航
```
←→ 表盘(0) ←→ 天气(1) ←→ 计时器(2) ←→ 2048(3) →→
```

---

## 三、内存与资源占用分析

### 3.1 Flash 分区使用 (16MB Flash)

| 分区 | 大小 | 实际占用 | 用途 |
|------|------|----------|------|
| bootloader | ~32KB | ~30KB | 启动引导 |
| nvs | 24KB | ~8KB | WiFi凭证存储 |
| phy_init | 4KB | ~4KB | PHY校准数据 |
| **factory (app)** | **~1.94MB** | **~1.23MB** | 固件主体 |
| vfs (fat) | 10MB | - | 预留文件系统 |
| storage (spiffs) | 4MB | - | 预留数据存储 |

### 3.2 固件大小构成（估算）

| 模块 | 估算Flash | 说明 |
|------|-----------|------|
| ESP-IDF 系统 (FreeRTOS, WiFi, TCP/IP) | ~400KB | 不可裁剪核心 |
| mbedTLS + 证书包 | ~100KB | **CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL** |
| LVGL 核心 + 字体 | ~200KB | 含 Montserrat 14/18/24/32/48 字体 |
| lwIP + HTTP Client | ~80KB | 网络协议栈 |
| cJSON | ~10KB | JSON解析 |
| 用户代码 (TFTLCD, UI, WiFi, Weather, Game) | ~30KB | 所有 components |
| **固件总大小** | **~1.23MB** | 剩余空间 ~700KB |

### 3.3 RAM 占用分析

| 项目 | 大小 | 说明 |
|------|------|------|
| **片上 SRAM (DRAM)** | ~512KB | ESP32-S3 内置 |
| **PSRAM (SPIRAM)** | 2MB/8MB | 外部 **OCTAL 模式, 80MHz** |
| LVGL 内存池 | **64KB** | `CONFIG_LV_MEM_SIZE_KILOBYTES=64` |
| LVGL 双缓冲 | **~46KB** | `240×48×2 ×2 = 46,080 bytes` (DMA) |
| LVGL 图层缓冲 | **24KB** | `CONFIG_LV_LAYER_SIMPLE_BUF_SIZE=24576` |
| FreeRTOS 任务栈 | ~38KB | 主任务16KB + WiFi 8KB + Weather 8KB + CST816T 3KB + 其他 |
| WiFi 驱动缓冲区 | ~60KB | 动态分配 (PSRAM) |
| HTTP 客户端缓冲 | ~2KB | weather.c 中 2048 bytes |
| **总计** | **~200KB+** | 含系统开销，PSRAM承担大块分配 |
| SPIRAM 内部预留 | 32KB | `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768` |
| SPIRAM 内部阈值 | 16KB | `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384` (≤16KB 走内部) |

### 3.4 LVGL 对象数量估算

| 页面 | 对象类型 | 估算数量 |
|------|----------|----------|
| 表盘(0) | labels, arc, panels(3), page dots(4) | ~20 |
| 天气(1) | labels, icon container, sun/moon(4 obj), panels(2), btn, spacer | ~25 |
| 计时器(2) | labels, btns(2), labels in btns | ~12 |
| 2048(3) | labels, tiles(16), board bg, overlay, btns(2) | ~30 |
| 全局 | tileview, scr, 未显示页面的对象 | ~15 |
| **总计** | | **~100+ LVGL对象** |

---

## 四、关键配置参数

### 4.1 显示与触摸

| 参数 | 值 | 位置 |
|------|-----|------|
| 屏幕分辨率 | 240×280 | tftlcd.h |
| SPI 时钟 | **60MHz** | tftlcd.c |
| LVGL 缓冲区行数 | **48** (240×48×2=23KB) | tftlcd.h `LCD_BUF_ROWS` |
| LVGL 色深 | **RGB565** (16-bit) | sdkconfig |
| 触摸轮询 | **10ms** (100Hz) | cst816t.c |
| 触摸 I2C | 400KHz, addr 0x15 | cst816t.c |

### 4.2 LVGL 配置

| 参数 | 值 | 说明 |
|------|-----|------|
| 默认刷新周期 | 30ms | `LV_DISP_DEF_REFR_PERIOD` |
| 输入读取周期 | 10ms | `LV_INDEV_DEF_READ_PERIOD` |
| 内存池 | 64KB | 可增大到128KB改善大屏刷新 |
| 阴影缓存 | 0 | 不使用阴影特效（省内存） |
| 圆形缓存 | 4 | 圆弧预计算 |
| 默认字体 | Montserrat 14 | `LV_FONT_DEFAULT` |
| 已启用字体 | **14, 18, 24, 32, 48** | 5种字体共约120KB Flash |
| 编译器优化 | `-Og` (Debug) | 可改为 `-Os` (Size) 减小固件 |

### 4.3 任务配置

| 任务 | 栈大小 | 优先级 | 功能 |
|------|--------|--------|------|
| main (LVGL) | **16KB** | 默认(1) | LVGL渲染 + 主循环 |
| wifi_init | **8KB** | 3 | WiFi初始化（一次性，完成后自删） |
| weather | **8KB** | 5 | HTTP天气请求（30分钟间隔/手动触发） |
| cst816t | **3KB** | 4 | 触摸轮询10ms |
| IDLE | 默认 | 0 | FreeRTOS空闲任务 |

---

## 五、可优化项

### 🔴 高优先级（功能/稳定性）

| # | 项目 | 现状 | 建议 |
|---|------|------|------|
| 1 | LVGL `%f` 不支持 | 用 `snprintf` + `lv_label_set_text` | 已解决，注意字符串缓冲区大小 |
| 2 | 2048 滑动冲突 | 触摸时锁 TileView | 当前方案有效，但"快速双指"场景未覆盖 |
| 3 | 首次 NTP 同步前 | 显示1970年时间 | 可加"Syncing..."占位或本地RTC缓存 |
| 4 | WiFi断连重连 | 自动重试5次 | 可增加指数退避、WiFi状态图标 |

### 🟡 中优先级（体验优化）

| # | 项目 | 现状 | 建议 |
|---|------|------|------|
| 5 | 天气图标 | 太阳/月亮仅分昼夜 | 可结合天气代码：阴天=灰太阳、雷雨=闪电 |
| 6 | 计时器 | 只有 Start/Stop/Reset | 可加 Lap (计次) 功能、暂停恢复动画 |
| 7 | 2048 动画 | 直接刷新（无过渡） | 可加 tile 移动/合并动画（`lv_anim`） |
| 8 | 2048 高分存储 | 无持久化 | 用 NVS 存储最高分 |
| 9 | 表盘步数/心率 | 模拟数据（固定算法） | 后续接入真实传感器 |
| 10 | 页面指示点 | 4个小圆点 | 可加页码数字、当前页高亮动画 |
| 11 | 响应速度 | 同步SPI刷新 | 可尝试异步DMA刷新（之前有bug需小心调试） |
| 12 | 固件大小 | 1.23MB | 关闭不需要的LVGL widget可减~30KB |

### 🟢 低优先级（扩展功能）

| # | 项目 | 现状 | 建议 |
|---|------|------|------|
| 13 | 多城市天气 | 固定经纬度(德阳) | 可加GPS或手动城市选择 |
| 14 | 天气详情 | 仅当前天气 | 可加未来3天预报、空气质量 |
| 15 | 更多应用 | 目前4页 | 闹钟、倒计时、计算器、日历 |
| 16 | 省电模式 | 无 | 抬腕亮屏、息屏显示、降低刷新率 |
| 17 | 蓝牙 | 未启用 | BT/BLE 通知同步、音乐控制 |
| 18 | 主题切换 | 固定配色 | 暗色/明亮/自定义主题 |

---

## 六、内存调节建议

### 如果想减小内存占用：

| 操作 | 效果 | 方法 |
|------|------|------|
| 减小 LVGL 双缓冲行数 | 节省 ~10-20KB DMA | `tftlcd.h` 中 `LCD_BUF_ROWS` 从48→32 |
| 减小 LVGL 内存池 | 节省 16-32KB | `menuconfig` → LVGL → Memory size 从64→48KB |
| 关闭不需要的字体 | 节省 ~30KB Flash/字体 | 只保留 14, 18, 48 (关闭24, 32) |
| 关闭不需要的 LVGL Widget | 节省 ~20KB Flash | 关闭 Calendar, Chart, Meter, Span, Canvas |
| 减小天气 HTTP 缓冲 | 节省 1KB | weather.c `buffer_size` 从 2048→1024 |

### 如果屏幕响应还是慢：

| 操作 | 效果 | 风险 |
|------|------|------|
| 提高 SPI 时钟到 80MHz | 刷新快 33% | ST7789 可能不稳定 |
| 提高 LVGL 刷新周期 | 更快检查更新 | `LV_DISP_DEF_REFR_PERIOD` 从30→20ms |
| 降低 FreeRTOS tick | 主循环延迟更精确 | 从100Hz→200Hz（功耗略增） |
| 启用异步 DMA 刷新 | CPU与SPI并行 | 之前有bug，需仔细调试 |

---

## 七、文件依赖关系

```
main.c
 ├── lvgl.h
 ├── tftlcd.h ─────── tftlcd.c (SPI, GPIO, ST7789寄存器)
 ├── cst816t.h ────── cst816t.c (I2C, 触摸轮询)
 ├── watch_ui.h ───── watch_ui.c
 │   ├── weather.h ─── weather.c (HTTP, JSON, 任务通知)
 │   ├── wifi_mgr.h ─── wifi_mgr.c (WiFi STA, SNTP)
 │   └── game_2048.h ── game_2048.c (4×4棋盘, 滑动逻辑)
 └── wifi_mgr.h
```

---

## 八、编译与烧录

```bash
# 编译
idf.py build

# 烧录（COM口根据实际调整）
idf.py -p COM3 flash

# 查看串口日志
idf.py -p COM3 monitor

# 一键编译+烧录+监视
idf.py -p COM3 flash monitor
```

---

> 📝 **文档维护**: 每次重大改动后请更新此文档。
> 可以通过 `idf.py size-components` 查看各组件的 Flash/RAM 占用详情。
