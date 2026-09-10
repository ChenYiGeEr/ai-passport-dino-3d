// main/fap_screenshot.c
// FAP_SCREENSHOT_V1 协议实现。
// 主机发来 "FAP_SCREENSHOT_V1\n" → 回:
//   FAP_SCREENSHOT_V1 320 240 RGB565LE 153600\n
//   <153600 字节 RGB565LE 像素, 行优先>
#include "fap_screenshot.h"
#include "render.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define CMD "FAP_SCREENSHOT_V1"

static const char *TAG = "fap_screenshot";

static void serial_write(const uint8_t *data, int len)
{
    while (len > 0) {
        int w = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(1000));
        if (w <= 0) break;
        data += w;
        len -= w;
    }
}

static void fap_task(void *arg)
{
    (void)arg;
    char buf[32];
    size_t n = 0;
    while (1) {
        uint8_t ch;
        int r = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(200));
        if (r != 1) continue;
        if (ch == '\n') {
            buf[n] = 0;
            if (strcmp(buf, CMD) == 0)
                render_dump_request(); // 游戏循环在下一帧 flush 后导出
            n = 0;
        } else if (n < sizeof(buf) - 1) {
            buf[n++] = (char)ch;
        } else {
            n = 0; // 超长行, 丢弃
        }
    }
}

void fap_screenshot_init(void)
{
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        esp_err_t err = usb_serial_jtag_driver_install(&cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "USB serial driver init failed: %s; screenshots disabled",
                     esp_err_to_name(err));
            return;
        }
    }

    // Console 默认使用轮询 I/O；切到同一个驱动，避免它和截图任务争用硬件 FIFO。
    usb_serial_jtag_vfs_use_driver();
    if (xTaskCreate(fap_task, "fap", 2048, NULL, 4, NULL) != pdPASS) {
        ESP_LOGW(TAG, "screenshot task init failed; screenshots disabled");
        return;
    }
    ESP_LOGI(TAG, "screenshot protocol ready");
}

// 由游戏循环调用: 有待处理的导出请求时, 发头 + 整帧像素
void fap_screenshot_pump(void)
{
    if (!render_dump_pending()) return;
    char hdr[64];
    int len = snprintf(hdr, sizeof(hdr), "%s %d %d RGB565LE %d\n",
                       CMD, RENDER_SCREEN_W, RENDER_SCREEN_H,
                       RENDER_SCREEN_W * RENDER_SCREEN_H * 2);
    serial_write((const uint8_t *)hdr, len);
    render_dump_frame(serial_write);
}
