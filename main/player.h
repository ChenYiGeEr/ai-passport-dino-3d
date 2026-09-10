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
} player_t;

void player_init(player_t *p, int ground_x, int ground_y);
void player_reset(player_t *p);

// 请求跳跃(仅在地面且未下蹲时生效;空中无效,只触发一次)。
void player_jump(player_t *p);

// 每帧更新。dt 秒; up_held/down_held 为本帧按键按住状态。
// speed_level 0..3 对应原版四档速度,影响跳跃初速与动画节奏。
void player_update(player_t *p, float dt, bool up_held, bool down_held, int speed_level);

// 取当前应渲染的精灵(含死亡/下蹲/跳跃姿态选择)。
const sprite_t *player_sprite(const player_t *p);

// 屏幕坐标下的绘制左上角。
void player_draw_pos(const player_t *p, int *x, int *y);

// 碰撞盒(已向内收缩, 比视觉精灵小, 贴近原版手感)。
void player_hitbox(const player_t *p, int *x, int *y, int *w, int *h);
