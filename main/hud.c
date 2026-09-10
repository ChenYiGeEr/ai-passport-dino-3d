// main/hud.c
// 3x5 像素字体 HUD。每字符 3 列 5 行, 用 5 个 3bit 行掩码表示, 绘制时放大 2 倍。
#include "hud.h"
#include "render.h"

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
