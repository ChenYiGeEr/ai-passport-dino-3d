// main/hud.h
// 像素风 HUD:分数、最高分、"HI" 标记、GAME OVER / PAUSED 提示文字。
// 全部用内置 3x5 像素字体绘制,不依赖 LVGL 字体。
#pragma once

#include <stdint.h>
#include <stdbool.h>

// 在右上角绘制 "HI 00123 00456"。blink=true 时当前分数闪烁(用于破纪录提示)。
void hud_draw_scores(uint32_t score, uint32_t hi_score, bool blink_on, uint16_t color);

// 屏幕中央绘制 "GAME OVER"。
void hud_draw_game_over(uint16_t color);

// 屏幕中央绘制 "PAUSED"。
void hud_draw_paused(uint16_t color);

// 通用: 在指定位置绘制一行文字(2 倍 3x5 字模), 返回结束 x。
int hud_text(const char *s, int x, int y, uint16_t color);
