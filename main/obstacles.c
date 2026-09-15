// main/obstacles.c
#include "obstacles.h"
#include "render.h"
#include "sprites.h"

#include <stdlib.h>

// 生成间隔: 原版按速度与随机数拉开; 这里用"最小间距 + 随机余量"(像素)
#define GAP_BASE_MIN   200.0f
#define GAP_PER_SPEED   60.0f  // 每档速度增加的最小间距
#define GAP_RANDOM     220.0f
#define SAFE_GAP_CHANCE 10
#define OBSTACLE_SPAWN_X (RENDER_SCREEN_W + 80) // 300px/s 时约 1.2s 可见提前量
// 翼龙出现的分数门槛(对齐 Chrome 原版 ~450 分出现)
// 不再要求速度档: 之前 PTERO_MIN_LEVEL=1 + 每 100 分只提速 4px/s,
// 导致要打到 ~1750 分才满足, 等效"永远不出现"。
#define PTERO_MIN_LEVEL 0
#define PTERO_MIN_SCORE 450
// 翼龙三种高度(相对地面线的抬升, K=2 精灵尺寸)
#define PTERO_H_LOW     8
#define PTERO_H_MID    20
#define PTERO_H_HIGH   35
// 仙人掌成组("尾巴")概率, 原版 chance_to_spawn_tail = [100, 25]
#define TAIL_CHANCE     25
// 红心生成: 每 1000 分保底一颗 + 每次生成障碍 2% 概率; 满心不出
#define HEART_MILESTONE 1000
#define HEART_CHANCE    2
// 红心高度: 70% 贴地, 30% 低空(需起跳吃)
#define HEART_LOW_RATIO 70
#define HEART_PULSE_MS  420
#define HEART_FLOAT_MS  700
#define HEART_WRAP_MS  2100

static obstacle_t s_pool[OBSTACLE_POOL];
static int s_ground_y;
static float s_next_gap;      // 距下一个障碍还需滚动的距离
static float s_since_spawn;   // 上次生成后已滚动的距离
static uint32_t s_heart_next; // 下一个保底红心的分数门槛
static uint32_t s_shield_next; // 下一个保底护盾的分数门槛
static scene_id_t s_scene_current = SCENE_DESERT;
static scene_id_t s_scene_next = SCENE_DESERT;
static uint8_t s_scene_mix;
static uint8_t s_shadow_opacity = 255;

void obstacles_set_shadow_opacity(uint8_t opacity) { s_shadow_opacity = opacity; }

static const sprite_t *CACTI[] = {
    &spr_cactus, &spr_cactus_tall, &spr_cactus_thin,
    &spr_fcactus, &spr_fcactus_tall, &spr_fcactus_thin,
};
static const sprite_t *CANYON_ROCKS[] = {
    &spr_canyon_spire_0, &spr_canyon_spire_1, &spr_canyon_spire_2,
};
static const sprite_t *OASIS_THORNS[] = {
    &spr_oasis_thorn_0, &spr_oasis_thorn_1, &spr_oasis_thorn_2,
};
static const sprite_t *VOLCANO_VENTS[] = {
    &spr_volcano_vent_0, &spr_volcano_vent_1, &spr_volcano_vent_2,
};

void obstacles_set_scene(scene_id_t current, scene_id_t next, uint8_t mix)
{
    s_scene_current = current;
    s_scene_next = next;
    s_scene_mix = mix;
}

static const sprite_t *scene_ground_sprite(scene_id_t scene, int variant)
{
    switch (scene) {
    case SCENE_CANYON: return CANYON_ROCKS[variant % 3];
    case SCENE_OASIS: return OASIS_THORNS[variant % 3];
    case SCENE_VOLCANO: return VOLCANO_VENTS[variant % 3];
    default: return CACTI[variant % 6];
    }
}

static int ground_size_slot(scene_id_t scene, int variant)
{
    return scene == SCENE_DESERT ? (variant % 6) % 3 : variant % 3;
}

static int ground_target_height(scene_id_t scene, int variant)
{
    int slot = ground_size_slot(scene, variant);
    return slot == 0 ? 24 : slot == 1 ? 40 : 56;
}

static void ground_scaled_size(const sprite_t *sp, scene_id_t scene, int variant,
                               int *w, int *h)
{
    *h = ground_target_height(scene, variant);
    *w = sp->w * *h / sp->h;
    if (*w < 1) *w = 1;
}

static bool ground_is_destructible(scene_id_t scene, int variant)
{
    if (scene == SCENE_CANYON) return false;
    return ground_size_slot(scene, variant) == 0;
}

static int cactus_variant_for_size(scene_id_t scene, int size_slot)
{
    size_slot = size_slot < 0 ? 0 : size_slot > 2 ? 2 : size_slot;
    if (scene == SCENE_DESERT)
        return (rand() % 2) * 3 + size_slot;
    return size_slot;
}

static obstacle_t *alloc_obstacle(void)
{
    for (int i = 0; i < OBSTACLE_POOL; i++)
        if (!s_pool[i].active) return &s_pool[i];
    return NULL;
}

static void heart_geometry(const obstacle_t *o, int *x, int *y, int *w, int *h)
{
    static const int8_t PULSE_EXTRA[8] = { 0, 1, 1, 2, 2, 1, 1, 0 };
    static const int8_t BOB_Y[10] = { 0, -1, -2, -2, -1, 0, 1, 2, 2, 1 };
    int elapsed_ms = (int)(o->anim_timer * 1000.0f);
    int pulse_frame = (elapsed_ms % HEART_PULSE_MS) * 8 / HEART_PULSE_MS;
    int bob_frame = (elapsed_ms % HEART_FLOAT_MS) * 10 / HEART_FLOAT_MS;
    int extra = PULSE_EXTRA[pulse_frame];
    int center_x = (int)o->x;
    int center_y = (int)o->y - OBS_HEART_BASE_H / 2 + BOB_Y[bob_frame];

    *w = OBS_HEART_BASE_W + extra;
    *h = OBS_HEART_BASE_H + extra;
    *x = center_x - *w / 2;
    *y = center_y - *h / 2;
}

void obstacles_init(int ground_y)
{
    s_ground_y = ground_y;
    obstacles_reset();
}

void obstacles_reset(void)
{
    for (int i = 0; i < OBSTACLE_POOL; i++) s_pool[i].active = false;
    s_since_spawn = 0;
    s_next_gap = 300; // 开局缓冲, 别一出生就撞
    s_heart_next = HEART_MILESTONE;
    s_shield_next = HEART_MILESTONE;
}

static float spawn_cactus(int speed_level)
{
    int count = 1;
    if (rand() % 100 < TAIL_CHANCE) {
        // 低速档最多 2 连(保证起跳距离内可过), 高速档才出现 3 连
        count = speed_level == 0 ? 2 : 2 + rand() % 2;
    }

    int sizes[3];
    sizes[0] = count == 1 ? 1 + rand() % 2 : rand() % 3;
    bool has_large_companion = sizes[0] > 0;
    for (int i = 1; i < count; i++) {
        sizes[i] = rand() % 3;
        if (sizes[i] > 0) has_large_companion = true;
    }
    // 小障碍只能作为组合的一部分，且必须配合同类型的中号或大号障碍。
    if (!has_large_companion)
        sizes[count - 1] = 1 + rand() % 2;

    int base_x = OBSTACLE_SPAWN_X;
    float group_width = 0;
    for (int i = 0; i < count; i++) {
        obstacle_t *o = alloc_obstacle();
        if (!o) return group_width;
        o->type = OBS_CACTUS;
        o->variant = cactus_variant_for_size(s_scene_current, sizes[i]);
        o->spr = scene_ground_sprite(s_scene_current, o->variant);
        o->x = (float)base_x;
        o->y = (float)s_ground_y;
        o->anim_timer = 0;
        o->anim_frame = 0;
        o->destructible = ground_is_destructible(s_scene_current, o->variant);
        o->active = true;
        int draw_w, draw_h;
        ground_scaled_size(o->spr, s_scene_current, o->variant, &draw_w, &draw_h);
        group_width += draw_w - 4;
        base_x += draw_w - 4; // 按实际尺寸轻微叠放，避免缩放后产生空洞
    }
    return group_width;
}

static void spawn_ptero(void)
{
    obstacle_t *o = alloc_obstacle();
    if (!o) return;
    o->type = OBS_PTERO;
    o->spr = &spr_ptero_0;
    o->spr2 = &spr_ptero_1;
    o->ptero_frames[0]=&spr_ptero_0; o->ptero_frames[1]=&spr_ptero_1;
    o->ptero_frames[2]=&spr_ptero_2; o->ptero_frames[3]=&spr_ptero_3;
    o->ptero_frames[4]=&spr_ptero_4; o->ptero_frames[5]=&spr_ptero_5;
    o->x = (float)OBSTACLE_SPAWN_X;
    // 头顶翼龙约占飞行障碍的 2/3，对应总生成权重约 20%。
    int h = (rand() % 3 == 0) ? (rand() % 3 == 0 ? 0 : 1 + rand() % 2) : 2;
    int lift = h == 0 ? PTERO_H_LOW : h == 1 ? PTERO_H_MID : PTERO_H_HIGH;
    o->y = (float)(s_ground_y - lift);
    o->anim_timer = 0;
    o->anim_frame = 0;
    o->destructible = false;
    o->active = true;
}

static void shield_geometry(const obstacle_t *o, int *x, int *y, int *w, int *h)
{
    static const int8_t BOB_Y[10] = { 0, -1, -2, -2, -1, 0, 1, 2, 2, 1 };
    int elapsed_ms = (int)(o->anim_timer * 1000.0f);
    int bob_frame = (elapsed_ms % HEART_FLOAT_MS) * 10 / HEART_FLOAT_MS;
    *w = OBS_SHIELD_BASE_W;
    *h = OBS_SHIELD_BASE_H;
    *x = (int)o->x - *w / 2;
    *y = (int)o->y - *h / 2 + BOB_Y[bob_frame];
}

static void draw_shield_icon(int x, int y, int w, int h, uint16_t color)
{
    int cx = x + w / 2;
    render_fill_rect(cx - 7, y, 14, 2, color);
    render_fill_rect(cx - 9, y + 2, 2, h / 2, color);
    render_fill_rect(cx + 7, y + 2, 2, h / 2, color);
    render_fill_rect(cx - 7, y + h / 2 - 2, 2, 6, color);
    render_fill_rect(cx + 5, y + h / 2 - 2, 2, 6, color);
    render_fill_rect(cx - 5, y + h - 8, 2, 4, color);
    render_fill_rect(cx + 3, y + h - 8, 2, 4, color);
    render_fill_rect(cx - 3, y + h - 4, 6, 2, color);
    render_fill_rect(cx - 1, y + 6, 2, 10, RGB565(80, 210, 255));
    render_fill_rect(cx - 5, y + 10, 10, 2, RGB565(80, 210, 255));
}
static void spawn_shield(void)
{
    obstacle_t *o = alloc_obstacle();
    if (!o) return;
    o->type = OBS_SHIELD;
    o->spr = NULL;
    o->spr2 = NULL;
    o->x = (float)OBSTACLE_SPAWN_X;
    int lift = (rand() % 100 < HEART_LOW_RATIO) ? 8 : 45;
    o->y = (float)(s_ground_y - lift);
    o->anim_timer = 0;
    o->anim_frame = 0;
    o->destructible = false;
    o->active = true;
}

static void spawn_heart(void)
{
    obstacle_t *o = alloc_obstacle();
    if (!o) return;
    o->type = OBS_HEART;
    o->spr = &spr_heart;
    o->spr2 = NULL;
    o->x = (float)OBSTACLE_SPAWN_X;
    int lift = (rand() % 100 < HEART_LOW_RATIO) ? 8 : 45;
    o->y = (float)(s_ground_y - lift);
    o->anim_timer = 0;
    o->anim_frame = 0;
    o->active = true;
}

void obstacles_update(float dt, float speed_px, uint32_t score, int speed_level, int lives,
                       bool has_shield)
{
    // 滚动与扑翼
    for (int i = 0; i < OBSTACLE_POOL; i++) {
        obstacle_t *o = &s_pool[i];
        if (!o->active) continue;
        o->x -= speed_px * dt;
        if (o->type == OBS_PTERO) {
            o->anim_timer += dt;
            if (o->anim_timer >= 0.07f) {
                o->anim_timer -= 0.07f;
                o->anim_frame = (o->anim_frame + 1) % 6;
            }
        } else if (o->type == OBS_HEART || o->type == OBS_SHIELD) {
            o->anim_timer += dt;
            if (o->anim_timer >= HEART_WRAP_MS / 1000.0f)
                o->anim_timer -= HEART_WRAP_MS / 1000.0f;
        }
        int w = o->type == OBS_HEART ? OBS_HEART_MAX_W
              : o->type == OBS_SHIELD ? OBS_SHIELD_BASE_W : o->spr->w;
        if (o->x + w < -20) o->active = false;
    }

    // 生成: 按滚动距离控制间隔, 间隔随速度档拉开
    s_since_spawn += speed_px * dt;
    if (s_since_spawn >= s_next_gap) {
        s_since_spawn = 0;
        float base_gap = GAP_BASE_MIN + GAP_PER_SPEED * speed_level
                       + (float)(rand() % (int)GAP_RANDOM);
        s_next_gap = base_gap;
        // 红心与护盾分别维护保底门槛；已有护盾时护盾保底不消耗。
        bool heart_due = score >= s_heart_next;
        bool shield_due = !has_shield && score >= s_shield_next;
        if (heart_due) s_heart_next = ((score / HEART_MILESTONE) + 1) * HEART_MILESTONE;
        if (!has_shield && (shield_due || rand() % 100 < HEART_CHANCE)) {
            if (shield_due) s_shield_next = ((score / HEART_MILESTONE) + 1) * HEART_MILESTONE;
            spawn_shield();
            return;
        }
        if (lives < 3 && (heart_due || rand() % 100 < HEART_CHANCE)) {
            spawn_heart();
            return;
        }
        if (rand() % 100 < SAFE_GAP_CHANCE) return;
        bool want_ptero = speed_level >= PTERO_MIN_LEVEL && score >= PTERO_MIN_SCORE
                          && rand() % 100 < 30;
        if (want_ptero) {
            spawn_ptero();
        } else {
            // s_since_spawn 从首个障碍开始计量，因此要把本波宽度加回去，
            // 保持波与波之间的可通过间距不因尺寸组合而缩短。
            s_next_gap = base_gap + spawn_cactus(speed_level);
        }
    }
}

void obstacles_draw(void)
{
    for (int i = 0; i < OBSTACLE_POOL; i++) {
        obstacle_t *o = &s_pool[i];
        if (!o->active) continue;
        if (o->type == OBS_SHIELD) {
            int sx, sy, sw, sh;
            shield_geometry(o, &sx, &sy, &sw, &sh);
            draw_shield_icon(sx, sy, sw, sh, RGB565(235, 235, 255));
            continue;
        }
        if (o->type == OBS_HEART) {
            int sx, sy, sw, sh;
            heart_geometry(o, &sx, &sy, &sw, &sh);
            render_sprite_scaled(&spr_heart, sx, sy, sw, sh, 255);
            continue;
        }
        const sprite_t *sp = o->type == OBS_PTERO ? o->ptero_frames[o->anim_frame % 6]
                                                  : scene_ground_sprite(s_scene_current, o->variant);
        int draw_w = sp->w, draw_h = sp->h;
        if (o->type == OBS_CACTUS)
            ground_scaled_size(sp, s_scene_current, o->variant, &draw_w, &draw_h);
        int sx = (int)o->x - draw_w / 2;
        int sy = (int)o->y - draw_h;
        // 椭圆阴影(伪 3D 关键线索, 对齐原版的 blob shadow); 红心贴地/悬浮不画
        if (o->type != OBS_HEART) {
            int sh_w = draw_w * 4 / 5;
            int sh_y = (o->type == OBS_PTERO) ? s_ground_y + 1 : (int)o->y + 1;
            render_set_opacity(s_shadow_opacity);
            render_fill_rect((int)o->x - sh_w / 2, sh_y, sh_w, 3, RGB565(120, 95, 45));
            render_set_opacity(255);
        }
        if (o->type == OBS_CACTUS && s_scene_mix > 0 && s_scene_next != s_scene_current) {
            const sprite_t *next = scene_ground_sprite(s_scene_next, o->variant);
            render_set_opacity((uint8_t)(255 - s_scene_mix));
            if (o->type == OBS_CACTUS)
                render_sprite_scaled(sp, (int)o->x - draw_w / 2,
                                     (int)o->y - draw_h, draw_w, draw_h,
                                     (uint8_t)(255 - s_scene_mix));
            else
                render_sprite(sp, sx, sy);
            render_set_opacity(s_scene_mix);
            int nw = next->w, nh = next->h;
            ground_scaled_size(next, s_scene_next, o->variant, &nw, &nh);
            render_sprite_scaled(next, (int)o->x - nw / 2,
                                 (int)o->y - nh, nw, nh, s_scene_mix);
            render_set_opacity(255);
        } else {
            if (o->type == OBS_CACTUS)
                render_sprite_scaled(sp, sx, sy, draw_w, draw_h, 255);
            else
                render_sprite(sp, sx, sy);
        }
    }
}

static void obs_hitbox(const obstacle_t *o, int *x, int *y, int *w, int *h)
{
    if (o->type == OBS_SHIELD) {
        shield_geometry(o, x, y, w, h);
        return;
    }
    if (o->type == OBS_HEART) {
        heart_geometry(o, x, y, w, h);
        return;
    }
    const sprite_t *sp = o->type == OBS_PTERO ? o->ptero_frames[o->anim_frame % 6]
                                              : scene_ground_sprite(s_scene_current, o->variant);
    *x = (int)o->x - sp->w / 2;
    *y = (int)o->y - sp->h;
    *w = sp->w;
    *h = sp->h;
    if (o->type == OBS_CACTUS) {
        int sw, sh;
        ground_scaled_size(sp, s_scene_current, o->variant, &sw, &sh);
        *x = (int)o->x - sw / 2 + sw / 8;
        *y = (int)o->y - sh + sh / 8;
        *w = sw - sw / 4;
        *h = sh - sh / 8 - 2;
    } else {
        *x += 11; *w -= 22;
        *y += 7; *h -= 14;
    }
}

int obstacles_collide(int px, int py, int pw, int ph)
{
    for (int i = 0; i < OBSTACLE_POOL; i++) {
        obstacle_t *o = &s_pool[i];
        if (!o->active) continue;
        int ox, oy, ow, oh;
        obs_hitbox(o, &ox, &oy, &ow, &oh);
        if (px < ox + ow && px + pw > ox && py < oy + oh && py + ph > oy)
            return i;
    }
    return -1;
}

obs_type_t obstacles_type(int idx)
{
    return s_pool[idx].type;
}

void obstacles_visual_center(int idx, int *x, int *y)
{
    if (idx < 0 || idx >= OBSTACLE_POOL || !s_pool[idx].active) {
        *x = 0;
        *y = 0;
        return;
    }
    int left, top, w, h;
    obs_hitbox(&s_pool[idx], &left, &top, &w, &h);
    *x = left + w / 2;
    *y = top + h / 2;
}

void obstacles_remove(int idx)
{
    if (idx >= 0 && idx < OBSTACLE_POOL) s_pool[idx].active = false;
}

bool obstacles_is_destructible(int idx)
{
    return idx >= 0 && idx < OBSTACLE_POOL && s_pool[idx].active &&
           s_pool[idx].type == OBS_CACTUS && s_pool[idx].destructible;
}

bool obstacles_stomp(int idx)
{
    if (!obstacles_is_destructible(idx)) return false;
    s_pool[idx].active = false;
    return true;
}
