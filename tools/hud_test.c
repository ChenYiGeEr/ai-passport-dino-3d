// tools/hud_test.c — 主机端渲染 HUD, 输出 PPM 供肉眼验证字模与排版。
// 用法: gcc -I main tools/hud_test.c main/hud.c -o /tmp/hud_test && /tmp/hud_test
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define W 320
#define H 240
static uint16_t fb[W * H];

// ---- mock render.h ----
#include "../main/render.h"
void render_sprite(const sprite_t *spr, int x, int y) { (void)spr; (void)x; (void)y; }
void render_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            if (xx >= 0 && yy >= 0 && xx < W && yy < H)
                fb[yy * W + xx] = color;
}
#include "../main/hud.h"

int main(void)
{
    memset(fb, 0xE0, sizeof(fb)); // 沙色底(近似)
    hud_draw_scores(42, 450, true, RGB565(60, 50, 30));
    hud_draw_game_over(RGB565(60, 50, 30));
    // 设置菜单文字样例(验证新增字形)
    hud_text("SETTINGS", 40, 150, RGB565(60, 50, 30));
    hud_text("VOLUME < 60% >", 40, 170, RGB565(60, 50, 30));
    hud_text("LIGHT < 80% >", 40, 190, RGB565(60, 50, 30));
    FILE *f = fopen("/tmp/hud-test.ppm", "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t v = fb[i];
        fputc(((v >> 11) & 31) * 255 / 31, f);
        fputc(((v >> 5) & 63) * 255 / 63, f);
        fputc((v & 31) * 255 / 31, f);
    }
    fclose(f);
    puts("/tmp/hud-test.ppm");
    return 0;
}
