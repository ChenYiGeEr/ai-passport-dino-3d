// main/render.h
// 条带式渲染器:绕过 LVGL,直接驱动 esp_lcd 面板。
// 320x240 横屏,每帧按若干 320xSTRIP_H 的横条合成并推送,避免全屏 framebuffer。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "sprites.h"
#include "scene.h"

#define RENDER_SCREEN_W  320
#define RENDER_SCREEN_H  240

// 初始化渲染器(分配 DMA 条带缓冲)。必须在 bsp_display_init() 之后调用。
void render_init(void);

// 设置精灵的夜间调色强度：0=白天原色，255=完整夜色。
void render_set_night_mix(uint8_t mix);
void render_set_scene_mix(scene_id_t current, scene_id_t next, uint8_t mix);

// 一帧开始:清空内部状态,准备按条带合成。
void render_begin(uint16_t sky_color, uint16_t ground_color, int ground_y);

// 在指定横屏坐标绘制精灵(自动裁剪)。x,y 为精灵左上角。
void render_sprite(const sprite_t *spr, int x, int y);

// 绘制不参与昼夜/场景精灵调色的图标，供高对比度 HUD 使用。
void render_sprite_raw(const sprite_t *spr, int x, int y);

// 以最近邻方式绘制到指定尺寸；opacity 用 4x4 有序抖动模拟 0..255 透明度。
void render_sprite_scaled(const sprite_t *spr, int x, int y, int w, int h,
                          uint8_t opacity);
void render_sprite_scaled_raw(const sprite_t *spr, int x, int y, int w, int h,
                              uint8_t opacity);

// 在指定坐标填充纯色矩形(自动裁剪)。
void render_fill_rect(int x, int y, int w, int h, uint16_t color);

// 设置后续精灵/矩形命令的有序抖动透明度；render_begin() 会恢复为 255。
void render_set_opacity(uint8_t opacity);

// 将此前合成的整屏内容按 RGB565 通道压暗；retain=128 约保留 50% 亮度。
void render_dim(uint8_t retain);

// 一帧结束:把所有条带推送到屏幕(阻塞至 DMA 发送完成)。
void render_flush(void);

// 仅把 start_y..屏幕底部推送到 LCD；命令列表仍保留完整帧，供截屏重放。
// start_y 可为任意像素行，函数会自动裁剪到屏幕范围。
void render_flush_from(int start_y);

// ---- 串口截屏导出(FAP_SCREENSHOT_V1) ----
// 标记下一帧需要导出。由串口任务调用。
void render_dump_request(void);
// 有待处理的导出请求时返回 true。
bool render_dump_pending(void);
// 重放当前帧命令, 逐条带回调写出整帧(RGB565 小端)。
// 必须在 render_flush() 之后调用(DMA 已空, s_cmds 还是本帧内容)。
typedef void (*render_dump_writer_t)(const uint8_t *data, int len);
void render_dump_frame(render_dump_writer_t writer);

// RGB565 颜色工具
#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
