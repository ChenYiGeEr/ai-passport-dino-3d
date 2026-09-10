// main/main.c
// ai-passport-dino: T-Rex Run 3D 的 ESP32-C3 (ST7789P3 240x320 横屏) 移植。
//
// 架构: 绕过 LVGL, 直接用 BSP 的 esp_lcd 面板句柄做条带渲染;
//       按键走 BSP 的 ADC 三键(Press 事件 + 电压轮询按住态);
//       Power 键独立于 ADC 按键矩阵, 本固件收不到它的事件。
#include "bsp_display.h"
#include "bsp_button.h"
#include "game.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "dino";

void app_main(void)
{
    // NVS(最高分持久化)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    if (bsp_display_init() != ESP_OK) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    bsp_display_backlight(80);

    // 进入游戏主循环(内部自带 FreeRTOS 节拍)
    game_run();
}
