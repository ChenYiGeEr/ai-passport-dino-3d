// main/scenery.c
#include "scenery.h"
#include "render.h"
#include "sprites.h"

#include <stdlib.h>
#include <math.h>

#define CLOUD_COUNT  3
#define DECOR_COUNT  4
#define FAR_COUNT    4
#define GROUND_FAR_COUNT  3
#define GROUND_NEAR_COUNT 2
#define TUMBLE_NEAR_COUNT 1
#define TUMBLE_MID_COUNT 2
#define STAR_COUNT  14
// 云的视差系数: 比地面慢, 营造远近层次
#define CLOUD_PARALLAX 0.25f
// 三层滚动视差系数
#define SCENE_MID_SPEED 0.75f
#define GROUND_NEAR_SPEED 1.10f

typedef struct { float x, y; int w; } cloud_t;
typedef struct {
    float x;
    const sprite_t *spr;
    int anchor_y;
} decor_t;
typedef struct {
    float x;
    float roll_distance;
    const sprite_t *spr;
    int anchor_y;
    bool small;
} tumbleweed_t;
typedef struct { float x, y; const sprite_t *spr; } far_t;
typedef struct { float x; int anchor_y; const sprite_t *spr; } depth_decor_t;
typedef struct { uint16_t x; uint8_t y, reveal, period, phase; } star_t;

static cloud_t s_clouds[CLOUD_COUNT];
static decor_t s_decor[DECOR_COUNT];
static tumbleweed_t s_tumble_near[TUMBLE_NEAR_COUNT];
static tumbleweed_t s_tumble_mid[TUMBLE_MID_COUNT];
static far_t s_far[FAR_COUNT];
static depth_decor_t s_ground_far[GROUND_FAR_COUNT];
static depth_decor_t s_ground_near[GROUND_NEAR_COUNT];
static int s_ground_y;
static int s_far_top, s_far_bottom; // 远河岸纵向范围
static int s_field_y;
static float s_speckle_off[3];
static float s_scene_mid_scroll;
static scene_id_t s_scene = SCENE_DESERT;

void scenery_set_scene(scene_id_t current) { s_scene = current; }

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
    &spr_cactus_mid_low_0, &spr_cactus_mid_low_1, &spr_cactus_mid_low_2,
    &spr_cactus_mid_high_0, &spr_cactus_mid_high_1, &spr_cactus_mid_high_2,
};
#define DECOR_NORMAL_COUNT ((int)(sizeof(DECOR_SPRITES) / sizeof(DECOR_SPRITES[0])))
#define DECOR_SPRITE_COUNT DECOR_NORMAL_COUNT

static const sprite_t *FAR_SPRITES[] = {
    &spr_dry_grass_far, &spr_agave_far,
};
#define FAR_BASE_COUNT ((int)(sizeof(FAR_SPRITES) / sizeof(FAR_SPRITES[0])))
#define FAR_SPRITE_COUNT FAR_BASE_COUNT

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
    c->y = (float)(16 + rand() % 49); // 云顶限制在 y=16..64
    c->w = 18 + rand() % 22;
}

static void spawn_decor(decor_t *d, bool initial)
{
    int choice = rand() % DECOR_SPRITE_COUNT;
    d->spr = DECOR_SPRITES[choice];
    d->anchor_y = s_field_y;
    d->x = initial ? (float)(rand() % RENDER_SCREEN_W)
                   : (float)(RENDER_SCREEN_W + 20 + rand() % 120);
}

static void spawn_tumbleweed(tumbleweed_t *t, bool near, bool initial)
{
    t->small = !near;
    t->roll_distance = 0;
    t->spr = TUMBLEWEED_FRAMES[0];
    t->anchor_y = near ? s_ground_y : s_field_y;
    t->x = initial ? (float)(rand() % RENDER_SCREEN_W)
                   : (float)(RENDER_SCREEN_W + (near ? 120 : 24)
                             + rand() % (near ? 220 : 180));
}

static void keep_tumbleweed_spacing(tumbleweed_t *group, int count)
{
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (fabsf(group[i].x - group[j].x) < 58.0f)
                group[j].x = group[i].x + 72.0f;
        }
    }
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
    const sprite_t *sp = FAR_SPRITES[choice];
    f->spr = sp;
    f->x = initial ? (float)(rand() % RENDER_SCREEN_W)
                   : (float)(RENDER_SCREEN_W + 30 + rand() % 150);
    // 底部贴着河岸线(略有几像素浮动), 高出的部分伸向天空
    f->y = (float)(s_far_bottom - 2 - (rand() % 6));
}

void scenery_init(int ground_y, int mid_top, int mid_bottom, int field_y)
{
    s_ground_y = ground_y;
    s_far_top = mid_top; (void)s_far_top; // 预留: 中景 y 目前只贴着 mid_bottom
    s_far_bottom = mid_bottom;
    s_field_y = field_y;
    s_scene_mid_scroll = 0;
    for (int i = 0; i < CLOUD_COUNT; i++) spawn_cloud(&s_clouds[i], true);
    for (int i = 0; i < DECOR_COUNT; i++) spawn_decor(&s_decor[i], true);
    for (int i = 0; i < TUMBLE_NEAR_COUNT; i++) spawn_tumbleweed(&s_tumble_near[i], true, true);
    for (int i = 0; i < TUMBLE_MID_COUNT; i++) spawn_tumbleweed(&s_tumble_mid[i], false, true);
    keep_tumbleweed_spacing(s_tumble_near, TUMBLE_NEAR_COUNT);
    keep_tumbleweed_spacing(s_tumble_mid, TUMBLE_MID_COUNT);
    for (int i = 0; i < FAR_COUNT; i++) spawn_far(&s_far[i], true);
    for (int i = 0; i < GROUND_FAR_COUNT; i++) spawn_depth_decor(&s_ground_far[i], false, true);
    for (int i = 0; i < GROUND_NEAR_COUNT; i++) spawn_depth_decor(&s_ground_near[i], true, true);
}

void scenery_reset(void)
{
    for (int i = 0; i < CLOUD_COUNT; i++) spawn_cloud(&s_clouds[i], true);
    for (int i = 0; i < DECOR_COUNT; i++) spawn_decor(&s_decor[i], true);
    for (int i = 0; i < TUMBLE_NEAR_COUNT; i++) spawn_tumbleweed(&s_tumble_near[i], true, true);
    for (int i = 0; i < TUMBLE_MID_COUNT; i++) spawn_tumbleweed(&s_tumble_mid[i], false, true);
    keep_tumbleweed_spacing(s_tumble_near, TUMBLE_NEAR_COUNT);
    keep_tumbleweed_spacing(s_tumble_mid, TUMBLE_MID_COUNT);
    for (int i = 0; i < FAR_COUNT; i++) spawn_far(&s_far[i], true);
    for (int i = 0; i < GROUND_FAR_COUNT; i++) spawn_depth_decor(&s_ground_far[i], false, true);
    for (int i = 0; i < GROUND_NEAR_COUNT; i++) spawn_depth_decor(&s_ground_near[i], true, true);
    for (int i = 0; i < 3; i++) s_speckle_off[i] = 0;
    s_scene_mid_scroll = 0;
}

void scenery_update(float dt, float speed_px)
{
    for (int i = 0; i < DECOR_COUNT; i++) {
        s_decor[i].x -= speed_px * SCENE_MID_SPEED * dt;
        if (s_decor[i].x + s_decor[i].spr->w < -20) spawn_decor(&s_decor[i], false);
    }
    tumbleweed_t *groups[2] = { s_tumble_near, s_tumble_mid };
    const int counts[2] = { TUMBLE_NEAR_COUNT, TUMBLE_MID_COUNT };
    for (int g = 0; g < 2; g++) {
        for (int i = 0; i < counts[g]; i++) {
            tumbleweed_t *t = &groups[g][i];
            float factor = t->small ? SCENE_MID_SPEED : GROUND_NEAR_SPEED;
            float travel = speed_px * dt * factor * TUMBLEWEED_SPEED;
            t->x -= travel;
            t->roll_distance += travel;
            int frame = (int)(t->roll_distance / TUMBLEWEED_FRAME_PX) % TUMBLEWEED_FRAME_COUNT;
            t->spr = TUMBLEWEED_FRAMES[frame];
            if (t->x + t->spr->w < -20) spawn_tumbleweed(t, !t->small, false);
        }
    }
    keep_tumbleweed_spacing(s_tumble_near, TUMBLE_NEAR_COUNT);
    keep_tumbleweed_spacing(s_tumble_mid, TUMBLE_MID_COUNT);
    for (int i = 0; i < FAR_COUNT; i++) {
        s_far[i].x -= speed_px * SCENE_MID_SPEED * dt;
        if (s_far[i].x + s_far[i].spr->w < -20) spawn_far(&s_far[i], false);
    }
    for (int i = 0; i < GROUND_NEAR_COUNT; i++) {
        depth_decor_t *d = &s_ground_near[i];
        d->x -= speed_px * GROUND_NEAR_SPEED * dt;
        if (d->x + d->spr->w < -20) spawn_depth_decor(d, true, false);
    }
    s_scene_mid_scroll += speed_px * dt * SCENE_MID_SPEED;
    while (s_scene_mid_scroll >= RENDER_SCREEN_W + 40) s_scene_mid_scroll -= RENDER_SCREEN_W + 40;
}

float scenery_scene_mid_scroll(void) { return s_scene_mid_scroll; }

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
                      bool is_night, float celestial_progress,
                      float game_time_s)
{
    if (celestial_progress < 0) celestial_progress = 0;
    if (celestial_progress > 1) celestial_progress = 1;
    int frame = ((uint32_t)(game_time_s * 8.0f)) & 3;
    float arc = sinf(celestial_progress * 3.1415926f);
    int x = is_night ? 16 + (int)(272.0f * celestial_progress + 0.5f)
                     : 288 - (int)(272.0f * celestial_progress + 0.5f);
    int y = 36 + (int)(8.0f * (1.0f - arc) + 0.5f);
    render_sprite_raw(is_night ?
                      (frame == 0 ? &spr_moon_0 : frame == 1 ? &spr_moon_1 :
                       frame == 2 ? &spr_moon_2 : &spr_moon_3) :
                      (frame == 0 ? &spr_sun_0 : frame == 1 ? &spr_sun_1 :
                       frame == 2 ? &spr_sun_2 : &spr_sun_3), x - 8, y - 8);
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

    // 夜间隐藏云层，让星星成为天空主体；白天/黄昏仍保留云。
    if (!is_night) {
        for (int i = 0; i < CLOUD_COUNT; i++) {
            cloud_t *c = &s_clouds[i];
            render_fill_rect((int)c->x, (int)c->y, c->w, 6, cloud_color);
            render_fill_rect((int)c->x + 4, (int)c->y - 4, c->w - 10, 6, cloud_color);
        }
    }
}

void scenery_draw_mid_back(void)
{
    if (s_scene != SCENE_DESERT) return;
    // 远景仅保留低细节草/龙舌兰；非碰撞仙人掌统一在中景绘制。
    for (int i = 0; i < FAR_COUNT; i++) {
        far_t *f = &s_far[i];
        render_sprite(f->spr, (int)f->x, (int)f->y - f->spr->h);
    }
}

void scenery_draw_ground_back(uint16_t speckle_color)
{
    (void)speckle_color; // 地面改为单一固定层，避免多速度纹理割裂。
    if (s_scene != SCENE_DESERT) return;
    for (int i = 0; i < DECOR_COUNT; i++) {
        decor_t *d = &s_decor[i];
        // 中景装饰底部对齐 MID 下边界，不进入 NEAR/碰撞地面。
        render_sprite(d->spr, (int)d->x, d->anchor_y - d->spr->h);
    }
    static const uint8_t BOUNCE[8] = { 0, 1, 2, 1, 0, 1, 2, 1 };
    for (int i = 0; i < TUMBLE_MID_COUNT; i++) {
        tumbleweed_t *t = &s_tumble_mid[i];
        int frame = (int)(t->roll_distance / TUMBLEWEED_FRAME_PX) % TUMBLEWEED_FRAME_COUNT;
        int w = t->spr->w * 7 / 10;
        int h = t->spr->h * 7 / 10;
        int bounce = BOUNCE[frame] / 2;
        render_sprite_scaled(t->spr, (int)t->x, t->anchor_y - h - bounce, w, h, 255);
    }
}

void scenery_draw_ground_front(void)
{
    if (s_scene != SCENE_DESERT) return;
    for (int i = 0; i < GROUND_NEAR_COUNT; i++) {
        depth_decor_t *d = &s_ground_near[i];
        render_sprite(d->spr, (int)d->x, d->anchor_y - d->spr->h / 2);
    }
    static const uint8_t BOUNCE[8] = { 0, 1, 2, 1, 0, 1, 2, 1 };
    // tumbleweed.vox 的外框上下有透明体素；按每帧可见像素底边补偿，
    // 并整体下移 3px，让 NEAR 风滚草更贴近地面而不显得飘浮。
    static const uint8_t VISIBLE_BOTTOM_PAD[8] = { 6, 5, 6, 6, 6, 2, 5, 7 };
    for (int i = 0; i < TUMBLE_NEAR_COUNT; i++) {
        tumbleweed_t *t = &s_tumble_near[i];
        int frame = (int)(t->roll_distance / TUMBLEWEED_FRAME_PX) % TUMBLEWEED_FRAME_COUNT;
        render_sprite(t->spr, (int)t->x,
                      t->anchor_y - t->spr->h + VISIBLE_BOTTOM_PAD[frame]
                      - BOUNCE[frame] + 3);
    }
}
