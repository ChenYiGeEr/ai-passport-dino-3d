// main/render.c
// 条带式渲染器实现。
//
// 思路:整帧 320x240 RGB565 需要 150KB,对 C3 太奢侈。改为每次只合成一个
// 320 x STRIP_H 的横条(15KB),合成完立即推屏,再合成下一条。绘制调用
// 记录在命令数组里,每条带重放一遍,代价可忽略。
#include "render.h"
#include "bsp_display.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define STRIP_H        24
#define MAX_CMDS       1024  // HUD 大字 + 计分板都是矩形命令, 256 会截断变乱码

// ST7789 经 esp_lcd 发送时要求大端字节序;若实机颜色红蓝互换, 把它改成 0 试试。
#define SWAP_BYTES     1

// ---- 横屏方向 ----
// 硬件按键布局: 左侧 Power, 右侧 Up/Down/OK。
// ST7789 纯旋转组合 (swap_xy, mirror_x, mirror_y):
//   0°   = (0,0,0)   90° = (1,1,0)
//   180° = (0,1,1)   270°= (1,0,1)
// ⚠ (1,1,1) 是 90°+水平镜像, 文字会变成 revoemag, 不要用。
//   LANDSCAPE_LEFT = 1 → 270°(1,0,1): 从左侧看横屏(Power 一侧朝下)
//   LANDSCAPE_LEFT = 0 → 90° (1,1,0): 从右侧看横屏
// 实机上下颠倒时, 只改这一个宏即可。
#define LANDSCAPE_LEFT 0

typedef enum { CMD_SPRITE, CMD_RECT } cmd_type_t;

typedef struct {
    cmd_type_t type;
    int x, y;
    union {
        struct { const sprite_t *spr; } s;
        struct { int w, h; uint16_t color; } r;
    };
} draw_cmd_t;

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_strip;                  // 320 * STRIP_H, DMA 内存
static draw_cmd_t s_cmds[MAX_CMDS];
static int s_cmd_count;
static uint8_t s_night_mix;                   // 精灵夜间调色强度(0..255)
static SemaphoreHandle_t s_trans_done;        // SPI DMA 传输完成信号

// 背景参数(本帧)
static uint16_t s_sky_color, s_ground_color;
static int s_ground_y;

// SPI 传输完成回调: 通知可以安全覆写条带缓冲
static bool on_trans_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *edata, void *user)
{
    (void)io; (void)edata; (void)user;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done, &hp);
    return hp == pdTRUE;
}

void render_init(void)
{
    s_panel = bsp_display_panel();
    // 横屏: 交换 X/Y, 物理 240x320 变逻辑 320x240; 纯旋转(见上方组合表)。
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_mirror(s_panel, LANDSCAPE_LEFT ? false : true,
                         LANDSCAPE_LEFT ? true : false);
    s_strip = heap_caps_malloc(RENDER_SCREEN_W * STRIP_H * sizeof(uint16_t),
                               MALLOC_CAP_DMA);

    // 注册传输完成回调: 修横条纹的关键——上一笔 DMA 没发完就覆写缓冲
    // 会导致屏幕上出现一道一道的横条纹。
    s_trans_done = xSemaphoreCreateBinary();
    xSemaphoreGive(s_trans_done);
    esp_lcd_panel_io_handle_t io = bsp_display_io();
    if (io) {
        const esp_lcd_panel_io_callbacks_t cbs = {
            .on_color_trans_done = on_trans_done,
        };
        esp_lcd_panel_io_register_event_callbacks(io, &cbs, NULL);
    }
}

void render_set_night_mix(uint8_t mix)
{
    s_night_mix = mix;
}

void render_begin(uint16_t sky_color, uint16_t ground_color, int ground_y)
{
    s_cmd_count = 0;
    s_sky_color = sky_color;
    s_ground_color = ground_color;
    s_ground_y = ground_y;
}

void render_sprite(const sprite_t *spr, int x, int y)
{
    if (s_cmd_count >= MAX_CMDS) return;
    draw_cmd_t *c = &s_cmds[s_cmd_count++];
    c->type = CMD_SPRITE;
    c->x = x; c->y = y;
    c->s.spr = spr;
}

void render_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (s_cmd_count >= MAX_CMDS) return;
    draw_cmd_t *c = &s_cmds[s_cmd_count++];
    c->type = CMD_RECT;
    c->x = x; c->y = y;
    c->r.w = w; c->r.h = h; c->r.color = color;
}

static inline uint16_t swap16(uint16_t v)
{
#if SWAP_BYTES
    return (v >> 8) | (v << 8);
#else
    return v;
#endif
}

// 把一条命令画进当前条带。strip_y 为条带在屏幕上的起始 y。
static void draw_cmd_in_strip(const draw_cmd_t *c, int strip_y)
{
    if (c->type == CMD_RECT) {
        int y0 = c->y > strip_y ? c->y : strip_y;
        int y1 = c->y + c->r.h < strip_y + STRIP_H ? c->y + c->r.h : strip_y + STRIP_H;
        int x0 = c->x > 0 ? c->x : 0;
        int x1 = c->x + c->r.w < RENDER_SCREEN_W ? c->x + c->r.w : RENDER_SCREEN_W;
        uint16_t v = swap16(c->r.color);
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                s_strip[(y - strip_y) * RENDER_SCREEN_W + x] = v;
        return;
    }
    // 精灵: 16bpp RGB565, 0x0000 透明; 按昼夜进度逐像素调向夜色
    const sprite_t *sp = c->s.spr;
    int x0 = c->x > 0 ? c->x : 0;
    int x1 = c->x + sp->w < RENDER_SCREEN_W ? c->x + sp->w : RENDER_SCREEN_W;
    int y0 = c->y > strip_y ? c->y : strip_y;
    int y1 = c->y + sp->h < strip_y + STRIP_H ? c->y + sp->h : strip_y + STRIP_H;
    for (int y = y0; y < y1; y++) {
        const uint16_t *row = sp->px + (y - c->y) * sp->w;
        uint16_t *dst = s_strip + (y - strip_y) * RENDER_SCREEN_W;
        for (int x = x0; x < x1; x++) {
            uint16_t v = row[x - c->x];
            if (!v) continue;
            if (s_night_mix) {
                // 压暗偏蓝: r×0.22 g×0.25 b×0.45
                int r = (v >> 11) & 31;
                int g = (v >> 5) & 63;
                int b = v & 31;
                int nr = r * 7 / 32;
                int ng = g * 8 / 64;
                int nb = b * 14 / 31 + 1;
                if (nb > 31) nb = 31;
                r += (nr - r) * s_night_mix / 255;
                g += (ng - g) * s_night_mix / 255;
                b += (nb - b) * s_night_mix / 255;
                v = (uint16_t)((r << 11) | (g << 5) | b);
            }
            dst[x] = swap16(v);
        }
    }
}

void render_flush(void)
{
    for (int sy = 0; sy < RENDER_SCREEN_H; sy += STRIP_H) {
        int rows = RENDER_SCREEN_H - sy < STRIP_H ? RENDER_SCREEN_H - sy : STRIP_H;
        // 先等上一笔 DMA 完成, 再覆写条带缓冲(顺序错了就是横条纹)
        xSemaphoreTake(s_trans_done, portMAX_DELAY);
        // 背景: 地平线上为天空色, 下为地面色
        for (int y = 0; y < rows; y++) {
            uint16_t v = swap16((sy + y) < s_ground_y ? s_sky_color : s_ground_color);
            for (int x = 0; x < RENDER_SCREEN_W; x++)
                s_strip[y * RENDER_SCREEN_W + x] = v;
        }
        for (int i = 0; i < s_cmd_count; i++)
            draw_cmd_in_strip(&s_cmds[i], sy);
        esp_lcd_panel_draw_bitmap(s_panel, 0, sy, RENDER_SCREEN_W, sy + rows, s_strip);
    }
    // 帧尾等最后一笔完成, 避免下一帧 begin 时缓冲还在线上
    xSemaphoreTake(s_trans_done, portMAX_DELAY);
    xSemaphoreGive(s_trans_done);
}

// ---- 串口截屏导出 ----
static volatile bool s_dump_pending;

void render_dump_request(void) { s_dump_pending = true; }
bool render_dump_pending(void) { return s_dump_pending; }

void render_dump_frame(render_dump_writer_t writer)
{
    s_dump_pending = false;
    for (int sy = 0; sy < RENDER_SCREEN_H; sy += STRIP_H) {
        int rows = RENDER_SCREEN_H - sy < STRIP_H ? RENDER_SCREEN_H - sy : STRIP_H;
        for (int y = 0; y < rows; y++) {
            uint16_t v = (sy + y) < s_ground_y ? s_sky_color : s_ground_color;
            for (int x = 0; x < RENDER_SCREEN_W; x++)
                s_strip[y * RENDER_SCREEN_W + x] = v;
        }
        for (int i = 0; i < s_cmd_count; i++)
            draw_cmd_in_strip(&s_cmds[i], sy);
        // s_strip 里存的是屏幕端字节序(可能已 swap), 导出为 RGB565LE 需还原
        int n = RENDER_SCREEN_W * rows;
#if SWAP_BYTES
        for (int i = 0; i < n; i++) s_strip[i] = swap16(s_strip[i]);
#endif
        writer((const uint8_t *)s_strip, n * 2);
    }
}
