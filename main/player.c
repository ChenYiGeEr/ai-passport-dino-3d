// main/player.c
#include "player.h"
#include "sprites.h"

// ---- 物理参数(像素域, 由原版 dino3d 单位域换算) ----
// 原版: jump.vel = 15/19/25(按速度档), gravity = -37;按住跳跃键有 1.1x boost。
// K=2 精灵下: 仙人掌高约 54px, 三连簇碰撞宽约 82px;
// 取跳高 ~68px, 空中 ~0.82s → 最低速 180px/s 下跳跃距离 ~148px, 可跳过三连。
#define JUMP_VEL_BASE   330.0f   // px/s, 速度档 0
#define JUMP_VEL_STEP    30.0f   // 每档增加的初速
#define GRAVITY         800.0f   // px/s^2
#define GRAVITY_BOOST   0.65f    // 按住 UP 上升期重力系数(跳得更高)
#define FAST_FALL_MULT    2.6f   // 空中按住 DOWN 速降
#define PLAYER_MIN_FOOT_Y 60.0f  // 横屏 240px 高度下，脚部最高到屏幕约 3/4 处
#define ANIM_BASE_S     0.090f   // 奔跑帧间隔(档 0)
#define ANIM_SPEEDUP    0.018f   // 每档减少的帧间隔

// 8 帧中 4/5 是收腿(跳跃)姿态, 奔跑步态用 2/6 两帧交替(左右跨步互为镜像)
static const sprite_t *RUN_FRAMES[] = {
    &spr_dino_0, &spr_dino_1, &spr_dino_2, &spr_dino_3,
    &spr_dino_4, &spr_dino_5, &spr_dino_6, &spr_dino_7,
    &spr_dino_8, &spr_dino_9, &spr_dino_10, &spr_dino_11,
};
static const sprite_t *DOWN_FRAMES[] = {
    &spr_dino_down_0, &spr_dino_down_1, &spr_dino_down_2, &spr_dino_down_3,
    &spr_dino_down_4, &spr_dino_down_5, &spr_dino_down_6, &spr_dino_down_7,
};
#define FRAME_COUNT 12
#define DOWN_FRAME_COUNT 8

static int s_ground_x, s_ground_y;

void player_init(player_t *p, int ground_x, int ground_y)
{
    s_ground_x = ground_x;
    s_ground_y = ground_y;
    player_reset(p);
}

void player_reset(player_t *p)
{
    p->x = (float)s_ground_x;
    p->y = (float)s_ground_y;
    p->vel_y = 0;
    p->jump_vel = JUMP_VEL_BASE;
    p->on_ground = true;
    p->crouching = false;
    p->dead = false;
    p->anim_frame = 0;
    p->anim_timer = 0;
    p->jump_buffer_s = 0;
}

bool player_jump(player_t *p)
{
    // 原版修复过"下蹲中起跳"的 bug:下蹲时不允许起跳
    if (!p->on_ground || p->crouching || p->dead) return false;
    p->vel_y = p->jump_vel;
    p->on_ground = false;
    return true;
}

void player_queue_jump(player_t *p)
{
    if (!p->dead) p->jump_buffer_s = PLAYER_JUMP_BUFFER_S;
}

player_event_t player_update(player_t *p, float dt, bool up_held,
                             bool down_held, int speed_level)
{
    if (p->dead) return PLAYER_EVENT_NONE;
    player_event_t events = PLAYER_EVENT_NONE;

    // 跳跃初速随速度档提高(原版: vel 15/19/25 随档位上升)
    p->jump_vel = JUMP_VEL_BASE + JUMP_VEL_STEP * speed_level;

    // 地面输入立即消费，不让本帧 dt 缩短刚按下的缓冲时间。
    p->crouching = down_held && p->on_ground;
    if (p->jump_buffer_s > 0 && player_jump(p)) {
        p->jump_buffer_s = 0;
        events = (player_event_t)(events | PLAYER_EVENT_JUMPED);
    }

    float buffered = p->jump_buffer_s;
    if (buffered > 0) {
        buffered -= dt;
        if (buffered < 0) buffered = 0;
    }

    // 竖直运动
    if (!p->on_ground) {
        float g = GRAVITY;
        if (up_held && p->vel_y > 0) g *= GRAVITY_BOOST;  // 按住跳更高
        if (down_held) g *= FAST_FALL_MULT;               // 空中速降
        p->vel_y -= g * dt;
        p->y -= p->vel_y * dt; // 屏幕 y 向下为正, vel_y 向上为正
        // 限制最高跳跃位置，避免高速度档把恐龙顶到屏幕外。
        if (p->y < PLAYER_MIN_FOOT_Y) {
            p->y = PLAYER_MIN_FOOT_Y;
            p->vel_y = 0;
        }
        if (p->y >= s_ground_y) {
            p->y = (float)s_ground_y;
            p->vel_y = 0;
            p->on_ground = true;
            events = (player_event_t)(events | PLAYER_EVENT_LANDED);
        }
    }

    // 落地后再刷新下蹲状态，然后消费仍有效的预输入。
    p->crouching = down_held && p->on_ground;
    p->jump_buffer_s = buffered;
    if (p->jump_buffer_s > 0 && player_jump(p)) {
        p->jump_buffer_s = 0;
        events = (player_event_t)(events | PLAYER_EVENT_JUMPED);
    }

    // 动画: 空中定格帧, 地面奔跑循环; 下蹲用下蹲帧循环
    float interval = ANIM_BASE_S - ANIM_SPEEDUP * speed_level;
    if (interval < 0.03f) interval = 0.03f;
    p->anim_timer += dt;
    if (p->anim_timer >= interval) {
        p->anim_timer = 0;
        int n = p->crouching ? DOWN_FRAME_COUNT : FRAME_COUNT;
        p->anim_frame = (p->anim_frame + 1) % n;
    }
    return events;
}

const sprite_t *player_sprite(const player_t *p)
{
    if (p->dead)
        return p->crouching ? &spr_dino_dead_down : &spr_dino_dead;
    if (p->crouching)
        return DOWN_FRAMES[p->anim_frame % DOWN_FRAME_COUNT];
    if (!p->on_ground)
        return &spr_dino_4; // 跳跃定格(收腿帧)
    return RUN_FRAMES[p->anim_frame % FRAME_COUNT];
}

void player_draw_pos(const player_t *p, int *x, int *y)
{
    const sprite_t *sp = player_sprite(p);
    // 锚点在精灵底部水平中心附近; 下蹲精灵更宽, 保持头部(右侧)位置不跳变
    bool wide = p->crouching || (p->dead && p->crouching);
    *x = (int)p->x - (wide ? 30 : 10);
    *y = (int)p->y - sp->h;
}

void player_hitbox(const player_t *p, int *x, int *y, int *w, int *h)
{
    const sprite_t *sp = player_sprite(p);
    int dx, dy;
    player_draw_pos(p, &dx, &dy);
    // 碰撞盒向内收 ~30%, 宽容贴图边缘的透明与阴影像素(贴近原版手感)
    int inset_x = sp->w * 3 / 10;
    int inset_top = sp->h / 8;
    int inset_bottom = 2;
    *x = dx + inset_x;
    *y = dy + inset_top;
    *w = sp->w - inset_x * 2;
    *h = sp->h - inset_top - inset_bottom;
}
