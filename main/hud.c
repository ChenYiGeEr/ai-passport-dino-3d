// main/hud.c
// 3x5 像素字体 HUD。每字符 3 列 5 行, 用 5 个 3bit 行掩码表示, 绘制时放大 2 倍。
#include "hud.h"
#include "render.h"

#include <stdio.h>
#include <string.h>

#define FONT_SCALE 2
#define CHAR_W     (3 * FONT_SCALE)
#define CHAR_H     (5 * FONT_SCALE)
#define CHAR_GAP   FONT_SCALE

// 3x5 字模: 每行 3bit, 低位在左
static const uint8_t FONT_DIGITS[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, // 0
    {0b010, 0b110, 0b010, 0b010, 0b111}, // 1
    {0b111, 0b001, 0b111, 0b100, 0b111}, // 2
    {0b111, 0b001, 0b111, 0b001, 0b111}, // 3
    {0b101, 0b101, 0b111, 0b001, 0b001}, // 4
    {0b111, 0b100, 0b111, 0b001, 0b111}, // 5
    {0b111, 0b100, 0b111, 0b101, 0b111}, // 6
    {0b111, 0b001, 0b001, 0b001, 0b001}, // 7
    {0b111, 0b101, 0b111, 0b101, 0b111}, // 8
    {0b111, 0b101, 0b111, 0b001, 0b111}, // 9
};

#define CH_A 10
#define CH_E 11
#define CH_G 12
#define CH_H 13
#define CH_I 14
#define CH_M 15
#define CH_O 16
#define CH_P 17
#define CH_R 18
#define CH_S 19
#define CH_U 20
#define CH_V 21
#define CH_D 22
#define CH_SPACE 23
#define CH_B 24
#define CH_C 25
#define CH_K 26
#define CH_L 27
#define CH_T 28
#define CH_PCT 29
#define CH_LT 30
#define CH_GT 31
#define CH_N 32
#define CH_F 33
#define CH_J 34
#define CH_Q 35
#define CH_W 36
#define CH_X 37
#define CH_Y 38
#define CH_DASH 39

static const uint8_t FONT_ALPHA[][5] = {
    {0b111, 0b101, 0b111, 0b101, 0b101}, // A
    {0b111, 0b100, 0b111, 0b100, 0b111}, // E
    {0b111, 0b100, 0b101, 0b101, 0b111}, // G
    {0b101, 0b101, 0b111, 0b101, 0b101}, // H
    {0b111, 0b010, 0b010, 0b010, 0b111}, // I
    {0b101, 0b111, 0b111, 0b101, 0b101}, // M
    {0b111, 0b101, 0b101, 0b101, 0b111}, // O
    {0b111, 0b101, 0b111, 0b100, 0b100}, // P
    {0b111, 0b101, 0b110, 0b101, 0b101}, // R
    {0b111, 0b100, 0b111, 0b001, 0b111}, // S
    {0b101, 0b101, 0b101, 0b101, 0b111}, // U
    {0b101, 0b101, 0b101, 0b101, 0b010}, // V
    {0b110, 0b101, 0b101, 0b101, 0b110}, // D
    {0b000, 0b000, 0b000, 0b000, 0b000}, // space
    {0b110, 0b101, 0b110, 0b101, 0b110}, // B
    {0b011, 0b100, 0b100, 0b100, 0b011}, // C
    {0b101, 0b101, 0b110, 0b101, 0b101}, // K
    {0b100, 0b100, 0b100, 0b100, 0b111}, // L
    {0b111, 0b010, 0b010, 0b010, 0b010}, // T
    {0b101, 0b001, 0b010, 0b100, 0b101}, // %
    {0b001, 0b010, 0b100, 0b010, 0b001}, // <
    {0b100, 0b010, 0b001, 0b010, 0b100}, // >
    {0b110, 0b111, 0b111, 0b101, 0b101}, // N
    {0b111, 0b100, 0b110, 0b100, 0b100}, // F
    {0b001, 0b001, 0b001, 0b101, 0b111}, // J
    {0b111, 0b101, 0b111, 0b011, 0b001}, // Q
    {0b101, 0b101, 0b111, 0b111, 0b101}, // W
    {0b101, 0b101, 0b010, 0b101, 0b101}, // X
    {0b101, 0b101, 0b010, 0b010, 0b010}, // Y
    {0b000, 0b000, 0b111, 0b000, 0b000}, // -
};

static void draw_glyph_scaled(int idx, int x, int y, int scale, uint16_t color)
{
    const uint8_t *rows = idx < 10 ? FONT_DIGITS[idx] : FONT_ALPHA[idx - 10];
    // 字模高位在左: 0b100 = 左列
    for (int r = 0; r < 5; r++)
        for (int col = 0; col < 3; col++)
            if (rows[r] & (0b100 >> col))
                render_fill_rect(x + col * scale, y + r * scale,
                                 scale, scale, color);
}

static void draw_glyph(int idx, int x, int y, uint16_t color)
{
    draw_glyph_scaled(idx, x, y, FONT_SCALE, color);
}

static int glyph_index(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c == ' ') return CH_SPACE;
    switch (c) {
    case 'A': return CH_A; case 'E': return CH_E; case 'G': return CH_G;
    case 'H': return CH_H; case 'I': return CH_I; case 'M': return CH_M;
    case 'O': return CH_O; case 'P': return CH_P; case 'R': return CH_R;
    case 'S': return CH_S; case 'U': return CH_U; case 'V': return CH_V;
    case 'D': return CH_D; case 'B': return CH_B; case 'C': return CH_C;
    case 'K': return CH_K; case 'L': return CH_L; case 'T': return CH_T;
    case '%': return CH_PCT; case '<': return CH_LT; case '>': return CH_GT;
    case 'N': return CH_N;
    case 'F': return CH_F; case 'J': return CH_J; case 'Q': return CH_Q;
    case 'W': return CH_W; case 'X': return CH_X; case 'Y': return CH_Y;
    case '-': return CH_DASH;
    default: return CH_SPACE;
    }
}

static int text_width(const char *s, int scale)
{
    return (int)strlen(s) * (3 * scale + scale);
}

static int draw_text(const char *s, int x, int y, uint16_t color)
{
    for (; *s; s++) {
        draw_glyph(glyph_index(*s), x, y, color);
        x += CHAR_W + CHAR_GAP;
    }
    return x;
}

// 大号文字 + 阴影, 用于 GAME OVER / PAUSED 这类中央提示
static void draw_text_big(const char *s, int y, uint16_t color, uint16_t shadow)
{
    const int scale = 3;
    int w = text_width(s, scale);
    int x = (RENDER_SCREEN_W - w) / 2;
    int cx = x;
    for (const char *p = s; *p; p++) {
        draw_glyph_scaled(glyph_index(*p), cx + 2, y + 2, scale, shadow);
        cx += 3 * scale + scale;
    }
    cx = x;
    for (const char *p = s; *p; p++) {
        draw_glyph_scaled(glyph_index(*p), cx, y, scale, color);
        cx += 3 * scale + scale;
    }
}

static int draw_number(uint32_t v, int digits, int x, int y, uint16_t color)
{
    char buf[12];
    for (int i = digits - 1; i >= 0; i--) { buf[i] = '0' + (v % 10); v /= 10; }
    buf[digits] = 0;
    return draw_text(buf, x, y, color);
}

void hud_draw_scores(uint32_t score, uint32_t hi_score, bool blink_on, uint16_t color)
{
    // 右上角: "HI 00450 00123"; 闪烁时跳过当前分数
    int x = RENDER_SCREEN_W - 10 - (2 + 1 + 5 + 1 + 5) * (CHAR_W + CHAR_GAP);
    x = draw_text("HI", x, 8, color);
    x += CHAR_GAP;
    x = draw_number(hi_score, 5, x, 8, color);
    x += CHAR_GAP;
    if (blink_on)
        draw_number(score, 5, x, 8, color);
}

static uint16_t battery_color(int soc)
{
    if (soc >= 0 && soc <= 10) return RGB565(230, 70, 55);
    if (soc >= 0 && soc <= 30) return RGB565(235, 150, 45);
    return RGB565(60, 190, 95);
}

void hud_draw_battery(int soc, bool charging, uint16_t color)
{
    const int x = RENDER_SCREEN_W - 74;
    const int y = 26;
    const int body_w = 20;
    const int body_h = 10;
    uint16_t fill = battery_color(soc);

    // 固定占位，避免读取失败或充电状态变化时 HUD 跳动。
    render_fill_rect(x, y, body_w, 2, color);
    render_fill_rect(x, y + body_h - 2, body_w, 2, color);
    render_fill_rect(x, y, 2, body_h, color);
    render_fill_rect(x + body_w - 2, y, 2, body_h, color);
    render_fill_rect(x + body_w, y + 3, 3, 4, color);
    if (soc >= 0) {
        int clamped = soc > 100 ? 100 : soc;
        int fill_w = (body_w - 4) * clamped / 100;
        if (fill_w > 0)
            render_fill_rect(x + 2, y + 2, fill_w, body_h - 4, fill);
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", clamped);
        hud_text(pct, x + 27, y, fill);
    } else {
        hud_text("--%", x + 27, y, color);
    }

    if (charging) {
        // 3x5 像素闪电，叠在电池主体左侧，避免增加布局宽度。
        uint16_t bolt = RGB565(245, 205, 55);
        render_fill_rect(x + 9, y - 2, 4, 4, bolt);
        render_fill_rect(x + 7, y + 2, 4, 4, bolt);
        render_fill_rect(x + 5, y + 6, 4, 4, bolt);
    }
}

void hud_draw_shield_scaled(int x, int y, int w, int h, uint16_t color)
{
    render_sprite_scaled(&spr_shield, x, y, w, h, 255);
}

void hud_draw_shield(int x, int y, uint16_t color)
{
    // HUD 稍大于红心(11x10)，视觉对齐：用 13x12
    render_sprite_scaled(&spr_shield, x, y, 14, 15, 255);
}

void hud_draw_game_over(uint16_t color)
{
    // 3 倍大字号 + 黑色阴影, 居中显示在屏幕上半部
    draw_text_big("GAME OVER", RENDER_SCREEN_H / 2 - 46, color, RGB565(0, 0, 0));
}

void hud_draw_paused(uint16_t color)
{
    draw_text_big("PAUSED", RENDER_SCREEN_H / 2 - 46, color, RGB565(0, 0, 0));
}

int hud_text(const char *s, int x, int y, uint16_t color)
{
    return draw_text(s, x, y, color);
}
