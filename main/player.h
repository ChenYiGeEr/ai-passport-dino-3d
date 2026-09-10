// main/player.h
// 小恐龙:奔跑/跳跃/下蹲动画与物理,碰撞盒输出。
// 物理参数按原版 dino3d(js/src/player_manager.js)的比例换算到屏幕像素。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "sprites.h"

typedef struct {
    float x, y;          // 脚底锚点(地面接触点)的屏幕坐标
    float vel_y;         // 竖直速度(px/s, 向上为正)
    float jump_vel;      // 当前速度档对应的跳跃初速
    bool on_ground;
    bool crouching;      // 按住 DOWN 时
    bool dead;
    int anim_frame;      // 当前动画帧
    float anim_timer;    // 帧切换计时
    float jump_buffer_s; // 落地前预输入的剩余有效时间
} player_t;

#define PLAYER_JUMP_BUFFER_S 0.080f

typedef enum {
    PLAYER_EVENT_NONE   = 0,
    PLAYER_EVENT_JUMPED = 1 << 0,
    PLAYER_EVENT_LANDED = 1 << 1,
} player_event_t;

void player_init(player_t *p, int ground_x, int ground_y);
void player_reset(player_t *p);

// 立即请求跳跃；成功时返回 true。
bool player_jump(player_t *p);

// 记录一次跳跃输入；若尚未落地，会在 80ms 内于落地瞬间自动起跳。
void player_queue_jump(player_t *p);

// 每帧更新。dt 秒; up_held/down_held 为本帧按键按住状态。
// speed_level 0..3 对应原版四档速度,影响跳跃初速与动画节奏。
player_event_t player_update(player_t *p, float dt, bool up_held,
                             bool down_held, int speed_level);

// 取当前应渲染的精灵(含死亡/下蹲/跳跃姿态选择)。
const sprite_t *player_sprite(const player_t *p);

// 屏幕坐标下的绘制左上角。
void player_draw_pos(const player_t *p, int *x, int *y);

// 碰撞盒(已向内收缩, 比视觉精灵小, 贴近原版手感)。
void player_hitbox(const player_t *p, int *x, int *y, int *w, int *h);
