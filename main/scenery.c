// main/scenery.c
#include "scenery.h"
#include "render.h"
#include "sprites.h"

#include <stdlib.h>

#define CLOUD_COUNT  3
#define DECOR_COUNT  4
#define FAR_COUNT    4
#define GROUND_FAR_COUNT  3
#define GROUND_NEAR_COUNT 2
#define STAR_COUNT  14
// 云的视差系数: 比地面慢, 营造远近层次
#define CLOUD_PARALLAX 0.25f
// 远景(树/小仙人掌)视差系数
#define FAR_PARALLAX   0.45f
#define GROUND_FAR_SPEED  0.45f
#define GROUND_MID_SPEED  0.85f
#define GROUND_NEAR_SPEED 1.20f

typedef struct { float x, y; int w; } cloud_t;
typedef struct {
    float x;
    float roll_distance;
    const sprite_t *spr;
    bool tumbleweed;
    int anchor_y;
} decor_t;
typedef struct { float x, y; const sprite_t *spr; } far_t;
typedef struct { float x; int anchor_y; const sprite_t *spr; } depth_decor_t;
typedef struct { uint16_t x; uint8_t y, reveal, period, phase; } star_t;

static cloud_t s_clouds[CLOUD_COUNT];
static decor_t s_decor[DECOR_COUNT];
static far_t s_far[FAR_COUNT];
static depth_decor_t s_ground_far[GROUND_FAR_COUNT];
static depth_decor_t s_ground_near[GROUND_NEAR_COUNT];
static int s_ground_y;
static int s_far_top, s_far_bottom; // 远河岸纵向范围
static int s_field_y;
static float s_speckle_off[3];

static const star_t STARS[STAR_COUNT] = {
    { 55, 18, 52, 11, 1 }, { 82, 45, 58, 13, 7 }, { 108, 27, 64, 17, 5 },
    { 136, 67, 70, 14, 9 }, { 160, 38, 76, 19, 2 }, { 188, 57, 82, 12, 6 },
    { 215, 42, 54, 16, 8 }, { 242, 69, 60, 15, 4 }, { 272, 51, 67, 18, 3 },
    { 302, 75, 74, 13, 10 }, { 66, 76, 80, 20, 12 }, { 119, 53, 86, 16, 11 },
    { 232, 20, 72, 21, 13 }, { 286, 32, 84, 17, 15 },
};

static const sprite_t *TUMBLEWEED_FRAMES[] = {
    &spr_tumbleweed_0, &spr_tumbleweed_1, &spr_tumbleweed_2, &spr_tumbleweed_3,
    &spr_tumbleweed_4, &spr_tumbleweed_5, &spr_tumbleweed_6, &spr_tumbleweed_7,
};
#define TUMBLEWEED_FRAME_COUNT 8
#define TUMBLEWEED_SPEED       1.15f
#define TUMBLEWEED_FRAME_PX   14.0f

static const sprite_t *DECOR_SPRITES[] = {
    &spr_rock_0, &spr_rock_1, &spr_rock_2, &spr_rock_3, &spr_rock_4,
    &spr_flower_0, &spr_flower_1, &spr_flower_2,
    &spr_scorpion, &spr_dry_grass,
};
#define DECOR_NORMAL_COUNT ((int)(sizeof(DECOR_SPRITES) / sizeof(DECOR_SPRITES[0])))
#define DECOR_SPRITE_COUNT (DECOR_NORMAL_COUNT + 1) // 普通精灵 + 1 个风滚草选项

static const sprite_t *FAR_SPRITES[] = {
    &spr_tree_green_far, &spr_tree_dead_far,
    &spr_cactus_far_0, &spr_cactus_far_1, &spr_cactus_far_2,
};
#define FAR_BASE_COUNT ((int)(sizeof(FAR_SPRITES) / sizeof(FAR_SPRITES[0])))
#define FAR_SPRITE_COUNT (FAR_BASE_COUNT + 1) // 最后一个槽位由两种新植物共享

static const sprite_t *GROUND_FAR_SPRITES[] = {
    &spr_rock_far_0, &spr_rock_far_2,
    &spr_flower_far_0, &spr_flower_far_2,
};
#define GROUND_FAR_SPRITE_COUNT 4

static const sprite_t *GROUND_NEAR_SPRITES[] = {
    &spr_rock_near_0, &spr_rock_near_2,
    &spr_flower_near_0, &spr_flower_near_2,
};
#define GROUND_NEAR_SPRITE_COUNT 4

static void spawn_cloud(cloud_t *c, bool initial)
{
    c->x = initial ? (float)(rand() % RENDER_SCREEN_W)
                   : (float)(RENDER_SCREEN_W + 20);
    c->y = (float)(10 + rand() % 60);
    c->w = 18 + rand() % 22;
}

static void spawn_decor(decor_t *d, bool initial)
{
    int choice = rand() % DECOR_SPRITE_COUNT;
    d->tumbleweed = choice == DECOR_SPRITE_COUNT - 1;
    d->roll_distance = 0;
    d->spr = d->tumbleweed ? TUMBLEWEED_FRAMES[0] : DECOR_SPRITES[choice];
    d->anchor_y = s_ground_y + 2 + rand() % 5;
    d->x = initial ? (float)(rand() % RENDER_SCREEN_W)
                   : (float)(RENDER_SCREEN_W + 20 + rand() % 120);
}

static void spawn_depth_decor(depth_decor_t *d, bool near, bool initial)
{
    const sprite_t *const *sprites = near ? GROUND_NEAR_SPRITES : GROUND_FAR_SPRITES;
    int count = near ? GROUND_NEAR_SPRITE_COUNT : GROUND_FAR_SPRITE_COUNT;
    d->spr = sprites[rand() % count];
    d->x = initial ? (float)(rand() % RENDER_SCREEN_W)
                   : (float)(RENDER_SCREEN_W + 30 + rand() % (near ? 180 : 120));
    if (near) {
        d->anchor_y = RENDER_SCREEN_H - 4 - rand() % 5;
    } else {
        int span = s_ground_y - s_field_y - 18;
        d->anchor_y = s_field_y + 9 + (span > 0 ? rand() % span : 0);
    }
}

static void spawn_far(far_t *f, bool initial)
{
    int choice = rand() % FAR_SPRITE_COUNT;
    const sprite_t *sp = choice < FAR_BASE_COUNT
        ? FAR_SPRITES[choice]
        : (rand() & 1) ? &spr_dry_grass_far : &spr_agave_far;
    f->spr = sp;
    f->x = initial ? (float)(rand() % RENDER_SCREEN_W)
                   : (float)(RENDER_SCREEN_W + 30 + rand() % 150);
    // 底部贴着河岸线(略有几像素浮动), 高出的部分伸向天空
    f->y = (float)(s_far_bottom - 2 - (rand() % 6));
}

void scenery_init(int ground_y, int far_top, int far_bottom, int field_y)
{
    s_ground_y = ground_y;
    s_far_top = far_top; (void)s_far_top; // 预留: 远景 y 目前只贴着 far_bottom
    s_far_bottom = far_bottom;
    s_field_y = field_y;
    for (int i = 0; i < CLOUD_COUNT; i++) spawn_cloud(&s_clouds[i], true);
    for (int i = 0; i < DECOR_COUNT; i++) spawn_decor(&s_decor[i], true);
    for (int i = 0; i < FAR_COUNT; i++) spawn_far(&s_far[i], true);
    for (int i = 0; i < GROUND_FAR_COUNT; i++) spawn_depth_decor(&s_ground_far[i], false, true);
    for (int i = 0; i < GROUND_NEAR_COUNT; i++) spawn_depth_decor(&s_ground_near[i], true, true);
}

void scenery_reset(void)
{
    for (int i = 0; i < CLOUD_COUNT; i++) spawn_cloud(&s_clouds[i], true);
    for (int i = 0; i < DECOR_COUNT; i++) spawn_decor(&s_decor[i], true);
    for (int i = 0; i < FAR_COUNT; i++) spawn_far(&s_far[i], true);
    for (int i = 0; i < GROUND_FAR_COUNT; i++) spawn_depth_decor(&s_ground_far[i], false, true);
    for (int i = 0; i < GROUND_NEAR_COUNT; i++) spawn_depth_decor(&s_ground_near[i], true, true);
    for (int i = 0; i < 3; i++) s_speckle_off[i] = 0;
}

void scenery_update(float dt, float speed_px)
{
    for (int i = 0; i < DECOR_COUNT; i++) {
        float travel = speed_px * dt * (s_decor[i].tumbleweed
                                        ? TUMBLEWEED_SPEED : GROUND_MID_SPEED);
        s_decor[i].x -= travel;
        if (s_decor[i].tumbleweed) {
            s_decor[i].roll_distance += travel;
            int frame = (int)(s_decor[i].roll_distance / TUMBLEWEED_FRAME_PX)
                        % TUMBLEWEED_FRAME_COUNT;
            s_decor[i].spr = TUMBLEWEED_FRAMES[frame];
        }
        if (s_decor[i].x + s_decor[i].spr->w < -20) spawn_decor(&s_decor[i], false);
    }
    for (int i = 0; i < FAR_COUNT; i++) {
        s_far[i].x -= speed_px * FAR_PARALLAX * dt;
        if (s_far[i].x + s_far[i].spr->w < -20) spawn_far(&s_far[i], false);
    }
    for (int i = 0; i < GROUND_FAR_COUNT; i++) {
        depth_decor_t *d = &s_ground_far[i];
        d->x -= speed_px * GROUND_FAR_SPEED * dt;
        if (d->x + d->spr->w < -12) spawn_depth_decor(d, false, false);
    }
    for (int i = 0; i < GROUND_NEAR_COUNT; i++) {
        depth_decor_t *d = &s_ground_near[i];
        d->x -= speed_px * GROUND_NEAR_SPEED * dt;
        if (d->x + d->spr->w < -20) spawn_depth_decor(d, true, false);
    }
    const float speeds[3] = { GROUND_FAR_SPEED, GROUND_MID_SPEED, GROUND_NEAR_SPEED };
    for (int i = 0; i < 3; i++) {
        s_speckle_off[i] += speed_px * speeds[i] * dt;
        while (s_speckle_off[i] >= RENDER_SCREEN_W + 32)
            s_speckle_off[i] -= RENDER_SCREEN_W + 32;
    }
}

void scenery_update_sky(float scroll_px)
{
    for (int i = 0; i < CLOUD_COUNT; i++) {
        s_clouds[i].x -= scroll_px * CLOUD_PARALLAX;
        if (s_clouds[i].x + s_clouds[i].w < -20)
            spawn_cloud(&s_clouds[i], false);
    }
}

int scenery_dynamic_top(void)
{
    int top = RENDER_SCREEN_H;
    for (int i = 0; i < FAR_COUNT; i++) {
        const far_t *f = &s_far[i];
        if (f->x >= RENDER_SCREEN_W || f->x + f->spr->w <= 0)
            continue;
        int object_top = (int)f->y - f->spr->h;
        if (object_top < top) top = object_top;
    }
    return top;
}

void scenery_draw_sky(uint16_t cloud_color, uint16_t star_dim,
                      uint16_t star_bright, float night_progress,
                      float game_time_s)
{
    int visible = (int)(night_progress * 100.0f + 0.5f);
    uint32_t tick = (uint32_t)(game_time_s * 10.0f);
    for (int i = 0; i < STAR_COUNT; i++) {
        const star_t *s = &STARS[i];
        if (visible < s->reveal) continue;
        uint32_t phase = (tick + s->phase) % s->period;
        bool bright = phase < 2 || phase + 2 >= s->period;
        if (bright && (i % 3 == 0)) {
            render_fill_rect(s->x - 1, s->y, 3, 1, star_bright);
            render_fill_rect(s->x, s->y - 1, 1, 3, star_bright);
        } else {
            render_fill_rect(s->x, s->y, bright ? 2 : 1, bright ? 2 : 1,
                             bright ? star_bright : star_dim);
        }
    }

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

static void draw_speckle_band(int count, int spacing, int y0, int y_span,
                              int w, int h, float offset, uint16_t color)
{
    int period = RENDER_SCREEN_W + 32;
    for (int i = 0; i < count; i++) {
        int x = (i * spacing - (int)offset) % period;
        if (x < 0) x += period;
        int y = y0 + (i * 7) % y_span;
        render_fill_rect(x, y, w, h, color);
    }
}

void scenery_draw_ground_back(uint16_t speckle_color)
{
    // 越靠近镜头，颗粒越大且移动越快。
    draw_speckle_band(9, 43, s_field_y + 4, s_ground_y - s_field_y - 9,
                      1, 1, s_speckle_off[0], speckle_color);
    draw_speckle_band(11, 37, s_ground_y + 4, 17,
                      3, 2, s_speckle_off[1], speckle_color);
    draw_speckle_band(8, 51, s_ground_y + 22, 11,
                      5, 3, s_speckle_off[2], speckle_color);

    for (int i = 0; i < GROUND_FAR_COUNT; i++) {
        depth_decor_t *d = &s_ground_far[i];
        render_sprite(d->spr, (int)d->x, d->anchor_y - d->spr->h / 2);
    }

    // 中景装饰仍位于玩法精灵之后；风滚草保留 K=2 的8帧滚动。
    for (int i = 0; i < DECOR_COUNT; i++) {
        decor_t *d = &s_decor[i];
        int bounce = 0;
        if (d->tumbleweed) {
            int frame = (int)(d->roll_distance / TUMBLEWEED_FRAME_PX)
                        % TUMBLEWEED_FRAME_COUNT;
            static const uint8_t BOUNCE[8] = { 0, 1, 2, 1, 0, 1, 2, 1 };
            bounce = BOUNCE[frame];
        }
        render_sprite(d->spr, (int)d->x, d->anchor_y - d->spr->h / 2 - bounce);
    }
}

void scenery_draw_ground_front(void)
{
    for (int i = 0; i < GROUND_NEAR_COUNT; i++) {
        depth_decor_t *d = &s_ground_near[i];
        render_sprite(d->spr, (int)d->x, d->anchor_y - d->spr->h / 2);
    }
}
