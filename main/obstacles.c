// main/obstacles.c
#include "obstacles.h"
#include "render.h"
#include "sprites.h"

#include <stdlib.h>

// 生成间隔: 原版按速度与随机数拉开; 这里用"最小间距 + 随机余量"(像素)
#define GAP_BASE_MIN   200.0f
#define GAP_PER_SPEED   60.0f  // 每档速度增加的最小间距
#define GAP_RANDOM     220.0f
// 翼龙出现的分数门槛(对齐 Chrome 原版 ~450 分出现)
// 不再要求速度档: 之前 PTERO_MIN_LEVEL=1 + 每 100 分只提速 4px/s,
// 导致要打到 ~1750 分才满足, 等效"永远不出现"。
#define PTERO_MIN_LEVEL 0
#define PTERO_MIN_SCORE 450
// 翼龙三种高度(相对地面线的抬升, K=2 精灵尺寸)
#define PTERO_H_LOW     8
#define PTERO_H_MID    40
#define PTERO_H_HIGH   70
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

static const sprite_t *CACTI[] = {
    &spr_cactus, &spr_cactus_tall, &spr_cactus_thin,
    &spr_fcactus, &spr_fcactus_tall, &spr_fcactus_thin,
};

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
}

static void spawn_cactus(int speed_level)
{
    int count = 1;
    if (rand() % 100 < TAIL_CHANCE) {
        // 低速档最多 2 连(保证起跳距离内可过), 高速档才出现 3 连
        count = speed_level == 0 ? 2 : 2 + rand() % 2;
    }
    int base_x = RENDER_SCREEN_W + 20;
    for (int i = 0; i < count; i++) {
        obstacle_t *o = alloc_obstacle();
        if (!o) return;
        o->type = OBS_CACTUS;
        o->spr = CACTI[rand() % 6];
        o->x = (float)base_x;
        o->y = (float)s_ground_y;
        o->anim_timer = 0;
        o->anim_frame = 0;
        o->active = true;
        base_x += o->spr->w - 4; // 稍微叠一点, 像原版成簇仙人掌
    }
}

static void spawn_ptero(void)
{
    obstacle_t *o = alloc_obstacle();
    if (!o) return;
    o->type = OBS_PTERO;
    o->spr = &spr_ptero_0;
    o->spr2 = &spr_ptero_1;
    o->x = (float)(RENDER_SCREEN_W + 20);
    int h = rand() % 3;
    o->y = (float)(s_ground_y - (h == 0 ? PTERO_H_LOW : h == 1 ? PTERO_H_MID : PTERO_H_HIGH));
    o->anim_timer = 0;
    o->anim_frame = 0;
    o->active = true;
}

static void spawn_heart(void)
{
    obstacle_t *o = alloc_obstacle();
    if (!o) return;
    o->type = OBS_HEART;
    o->spr = &spr_heart;
    o->spr2 = NULL;
    o->x = (float)(RENDER_SCREEN_W + 20);
    // 70% 贴地(离地 8px), 30% 低空(离地 45px, 需起跳)
    int lift = (rand() % 100 < HEART_LOW_RATIO) ? 8 : 45;
    o->y = (float)(s_ground_y - lift);
    o->anim_timer = 0;
    o->anim_frame = 0;
    o->active = true;
}

void obstacles_update(float dt, float speed_px, uint32_t score, int speed_level, int lives)
{
    // 滚动与扑翼
    for (int i = 0; i < OBSTACLE_POOL; i++) {
        obstacle_t *o = &s_pool[i];
        if (!o->active) continue;
        o->x -= speed_px * dt;
        if (o->type == OBS_PTERO) {
            o->anim_timer += dt;
            if (o->anim_timer >= 0.18f) { // 原版 ptero_anim_speed=0.10 缩放后
                o->anim_timer = 0;
                o->anim_frame ^= 1;
            }
        } else if (o->type == OBS_HEART) {
            o->anim_timer += dt;
            if (o->anim_timer >= HEART_WRAP_MS / 1000.0f)
                o->anim_timer -= HEART_WRAP_MS / 1000.0f;
        }
        int w = o->type == OBS_HEART ? OBS_HEART_MAX_W : o->spr->w;
        if (o->x + w < -20) o->active = false;
    }

    // 生成: 按滚动距离控制间隔, 间隔随速度档拉开
    s_since_spawn += speed_px * dt;
    if (s_since_spawn >= s_next_gap) {
        s_since_spawn = 0;
        s_next_gap = GAP_BASE_MIN + GAP_PER_SPEED * speed_level
                     + (float)(rand() % (int)GAP_RANDOM);
        // 红心: 里程碑保底 + 低概率随机, 满心不出
        bool heart_due = score >= s_heart_next;
        if (heart_due) s_heart_next = ((score / HEART_MILESTONE) + 1) * HEART_MILESTONE;
        if (lives < 3 && (heart_due || rand() % 100 < HEART_CHANCE)) {
            spawn_heart();
            return;
        }
        bool want_ptero = speed_level >= PTERO_MIN_LEVEL && score >= PTERO_MIN_SCORE
                          && rand() % 100 < 35;
        if (want_ptero) spawn_ptero();
        else spawn_cactus(speed_level);
    }
}

void obstacles_draw(void)
{
    for (int i = 0; i < OBSTACLE_POOL; i++) {
        obstacle_t *o = &s_pool[i];
        if (!o->active) continue;
        const sprite_t *sp = (o->type == OBS_PTERO && o->anim_frame) ? o->spr2 : o->spr;
        if (o->type == OBS_HEART) {
            int sx, sy, sw, sh;
            heart_geometry(o, &sx, &sy, &sw, &sh);
            render_sprite_scaled(sp, sx, sy, sw, sh, 255);
            continue;
        }
        int sx = (int)o->x - sp->w / 2;
        int sy = (int)o->y - sp->h;
        // 椭圆阴影(伪 3D 关键线索, 对齐原版的 blob shadow); 红心贴地/悬浮不画
        if (o->type != OBS_HEART) {
            int sh_w = sp->w * 4 / 5;
            int sh_y = (o->type == OBS_PTERO) ? s_ground_y + 1 : (int)o->y + 1;
            render_fill_rect((int)o->x - sh_w / 2, sh_y, sh_w, 3, RGB565(120, 95, 45));
        }
        render_sprite(sp, sx, sy);
    }
}

static void obs_hitbox(const obstacle_t *o, int *x, int *y, int *w, int *h)
{
    const sprite_t *sp = (o->type == OBS_PTERO && o->anim_frame) ? o->spr2 : o->spr;
    if (o->type == OBS_HEART) {
        heart_geometry(o, x, y, w, h);
        return;
    }
    *x = (int)o->x - sp->w / 2;
    *y = (int)o->y - sp->h;
    *w = sp->w;
    *h = sp->h;
    // 仙人掌视觉上有盆/阴影, 向内收一点
    if (o->type == OBS_CACTUS) {
        *x += 4; *w -= 8;
        *h -= 4;
    } else {
        *x += 8; *w -= 16; // 翼龙翅膀不算碰撞
        *y += 6; *h -= 12;
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
