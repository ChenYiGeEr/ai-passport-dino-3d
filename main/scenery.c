// main/scenery.c
#include "scenery.h"
#include "render.h"
#include "sprites.h"

#include <stdlib.h>

#define CLOUD_COUNT  3
#define DECOR_COUNT  4
#define FAR_COUNT    4
// 云的视差系数: 比地面慢, 营造远近层次
#define CLOUD_PARALLAX 0.25f
// 远景(树/小仙人掌)视差系数
#define FAR_PARALLAX   0.45f

typedef struct { float x, y; int w; } cloud_t;
typedef struct { float x; const sprite_t *spr; } decor_t;
typedef struct { float x, y; const sprite_t *spr; } far_t;

static cloud_t s_clouds[CLOUD_COUNT];
static decor_t s_decor[DECOR_COUNT];
static far_t s_far[FAR_COUNT];
static int s_ground_y;
static int s_far_top, s_far_bottom; // 远河岸纵向范围
static float s_speckle_off; // 地面斑点滚动偏移

static const sprite_t *DECOR_SPRITES[] = {
    &spr_rock_0, &spr_rock_1, &spr_rock_2, &spr_rock_3, &spr_rock_4,
    &spr_flower_0, &spr_flower_1, &spr_flower_2,
    &spr_scorpion, &spr_tumbleweed,
    // 骷髅头只保留 K=1 小号远景版(spr_skull_far), 不在近景出现
};
#define DECOR_SPRITE_COUNT 10

static const sprite_t *FAR_SPRITES[] = {
    &spr_tree_green_far, &spr_tree_dead_far,
    &spr_cactus_far_0, &spr_cactus_far_1, &spr_cactus_far_2,
    &spr_skull_far,
};
#define FAR_SPRITE_COUNT 6

static void spawn_cloud(cloud_t *c, bool initial)
{
    c->x = initial ? (float)(rand() % RENDER_SCREEN_W)
                   : (float)(RENDER_SCREEN_W + 20);
    c->y = (float)(10 + rand() % 60);
    c->w = 18 + rand() % 22;
}

static void spawn_decor(decor_t *d, bool initial)
{
    const sprite_t *sp = DECOR_SPRITES[rand() % DECOR_SPRITE_COUNT];
    d->spr = sp;
    d->x = initial ? (float)(rand() % RENDER_SCREEN_W)
                   : (float)(RENDER_SCREEN_W + 20 + rand() % 120);
}

static void spawn_far(far_t *f, bool initial)
{
    const sprite_t *sp = FAR_SPRITES[rand() % FAR_SPRITE_COUNT];
    f->spr = sp;
    f->x = initial ? (float)(rand() % RENDER_SCREEN_W)
                   : (float)(RENDER_SCREEN_W + 30 + rand() % 150);
    // 底部贴着河岸线(略有几像素浮动), 高出的部分伸向天空
    f->y = (float)(s_far_bottom - 2 - (rand() % 6));
}

void scenery_init(int ground_y, int far_top, int far_bottom)
{
    s_ground_y = ground_y;
    s_far_top = far_top; (void)s_far_top; // 预留: 远景 y 目前只贴着 far_bottom
    s_far_bottom = far_bottom;
    for (int i = 0; i < CLOUD_COUNT; i++) spawn_cloud(&s_clouds[i], true);
    for (int i = 0; i < DECOR_COUNT; i++) spawn_decor(&s_decor[i], true);
    for (int i = 0; i < FAR_COUNT; i++) spawn_far(&s_far[i], true);
}

void scenery_reset(void)
{
    for (int i = 0; i < CLOUD_COUNT; i++) spawn_cloud(&s_clouds[i], true);
    for (int i = 0; i < DECOR_COUNT; i++) spawn_decor(&s_decor[i], true);
    for (int i = 0; i < FAR_COUNT; i++) spawn_far(&s_far[i], true);
    s_speckle_off = 0;
}

void scenery_update(float dt, float speed_px)
{
    for (int i = 0; i < CLOUD_COUNT; i++) {
        s_clouds[i].x -= speed_px * CLOUD_PARALLAX * dt;
        if (s_clouds[i].x + s_clouds[i].w < -20) spawn_cloud(&s_clouds[i], false);
    }
    for (int i = 0; i < DECOR_COUNT; i++) {
        s_decor[i].x -= speed_px * dt;
        if (s_decor[i].x + s_decor[i].spr->w < -20) spawn_decor(&s_decor[i], false);
    }
    for (int i = 0; i < FAR_COUNT; i++) {
        s_far[i].x -= speed_px * FAR_PARALLAX * dt;
        if (s_far[i].x + s_far[i].spr->w < -20) spawn_far(&s_far[i], false);
    }
    s_speckle_off += speed_px * dt;
    if (s_speckle_off >= 8) s_speckle_off -= 8; // 斑点图案周期 8px
}

void scenery_draw_sky(uint16_t cloud_color)
{
    // 云: 两个矩形拼的像素团
    for (int i = 0; i < CLOUD_COUNT; i++) {
        cloud_t *c = &s_clouds[i];
        render_fill_rect((int)c->x, (int)c->y, c->w, 6, cloud_color);
        render_fill_rect((int)c->x + 4, (int)c->y - 4, c->w - 10, 6, cloud_color);
    }
}

void scenery_draw_far(void)
{
    // 远景树/小仙人掌: 底部对齐远河岸上的 y
    for (int i = 0; i < FAR_COUNT; i++) {
        far_t *f = &s_far[i];
        render_sprite(f->spr, (int)f->x, (int)f->y - f->spr->h);
    }
}

void scenery_draw_ground(void)
{
    // 地面斑点: 随速度滚动的小点, 营造地面移动感
    for (int i = 0; i < 14; i++) {
        int x = ((i * 53) - (int)s_speckle_off * 7) % (RENDER_SCREEN_W + 16);
        if (x < 0) x += RENDER_SCREEN_W + 16;
        int y = s_ground_y + 6 + (i * 7) % (RENDER_SCREEN_H - s_ground_y - 12);
        render_fill_rect(x, y, 3, 2, RGB565(180, 150, 90));
    }
    // 地面装饰精灵
    for (int i = 0; i < DECOR_COUNT; i++) {
        decor_t *d = &s_decor[i];
        render_sprite(d->spr, (int)d->x, s_ground_y + 4 - d->spr->h / 2);
    }
}
