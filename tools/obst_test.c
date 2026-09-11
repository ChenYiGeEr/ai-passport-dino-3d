// tools/obst_test.c — 主机端仿真障碍物生成, 统计翼龙出现频率。
// 用法: gcc -I main tools/obst_test.c main/sprites.c -o /tmp/obst_test && /tmp/obst_test
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// mock render
#include "../main/render.h"
void render_sprite(const sprite_t *spr, int x, int y) { (void)spr; (void)x; (void)y; }
void render_sprite_scaled(const sprite_t *spr, int x, int y, int w, int h,
                          uint8_t opacity)
{ (void)spr; (void)x; (void)y; (void)w; (void)h; (void)opacity; }
void render_fill_rect(int x, int y, int w, int h, uint16_t color)
{ (void)x; (void)y; (void)w; (void)h; (void)color; }
void render_set_opacity(uint8_t opacity) { (void)opacity; }

// 直接包含实现文件, 拿到 static 内部状态
#include "../main/obstacles.c"

int main(void)
{
    srand(1234);
    obstacles_init(205);

    int cactus_spawned = 0, ptero_spawned = 0, heart_spawned = 0;
    bool prev_active[OBSTACLE_POOL] = {0};
    int lives = 2; // 非满心, 红心应能生成

    // 模拟 120 秒游戏: 分数 1200, 速度 300px/s, 档 2
    float dt = 0.02f;
    for (int step = 0; step < 6000; step++) {
        obstacles_update(dt, 300.0f, 1200, 2, lives);
        for (int i = 0; i < OBSTACLE_POOL; i++) {
            if (s_pool[i].active && !prev_active[i]) {
                if (s_pool[i].type == OBS_PTERO) ptero_spawned++;
                else if (s_pool[i].type == OBS_HEART) heart_spawned++;
                else cactus_spawned++;
            }
            prev_active[i] = s_pool[i].active;
        }
    }
    printf("120s @score1200 lives2: cactus=%d ptero=%d heart=%d\n",
           cactus_spawned, ptero_spawned, heart_spawned);

    // 满心时红心不应出现
    obstacles_reset();
    int hearts_at_full = 0;
    memset(prev_active, 0, sizeof(prev_active));
    for (int step = 0; step < 6000; step++) {
        obstacles_update(dt, 300.0f, 1200, 2, 3);
        for (int i = 0; i < OBSTACLE_POOL; i++) {
            if (s_pool[i].active && !prev_active[i] && s_pool[i].type == OBS_HEART)
                hearts_at_full++;
            prev_active[i] = s_pool[i].active;
        }
    }
    printf("120s @full lives: heart=%d (expect 0)\n", hearts_at_full);

    // 红心的绘制框和碰撞框共用同一几何，并随脉动逐帧变化。
    obstacles_reset();
    spawn_heart();
    obstacle_t *heart = NULL;
    for (int i = 0; i < OBSTACLE_POOL; i++)
        if (s_pool[i].active && s_pool[i].type == OBS_HEART) heart = &s_pool[i];
    if (!heart) return 1;
    int x0, y0, w0, h0;
    obs_hitbox(heart, &x0, &y0, &w0, &h0);
    if (w0 != OBS_HEART_BASE_W || h0 != OBS_HEART_BASE_H) return 2;
    heart->anim_timer = 0.21f;
    int x1, y1, w1, h1;
    obs_hitbox(heart, &x1, &y1, &w1, &h1);
    if (w1 != OBS_HEART_MAX_W || h1 != OBS_HEART_MAX_H) return 3;
    if (x1 + w1 / 2 != x0 + w0 / 2) return 4;
    return 0;
}
