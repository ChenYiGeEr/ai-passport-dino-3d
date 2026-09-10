// main/render.h
// 条带式渲染器:绕过 LVGL,直接驱动 esp_lcd 面板。
// 320x240 横屏,每帧按若干 320xSTRIP_H 的横条合成并推送,避免全屏 framebuffer。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "sprites.h"

#define RENDER_SCREEN_W  320
#define RENDER_SCREEN_H  240

// 初始化渲染器(分配 DMA 条带缓冲)。必须在 bsp_display_init() 之后调用。
void render_init(void);

// 选择日/夜调色板(影响所有精灵绘制)。
void render_set_night(bool night);

// 一帧开始:清空内部状态,准备按条带合成。
void render_begin(uint16_t sky_color, uint16_t ground_color, int ground_y);

// 在指定横屏坐标绘制精灵(自动裁剪)。x,y 为精灵左上角。
void render_sprite(const sprite_t *spr, int x, int y);

// 在指定坐标填充纯色矩形(自动裁剪)。
void render_fill_rect(int x, int y, int w, int h, uint16_t color);

// 一帧结束:把所有条带推送到屏幕(阻塞至 DMA 发送完成)。
void render_flush(void);

// RGB565 颜色工具
#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
