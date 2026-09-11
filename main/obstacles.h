// main/obstacles.h
// 障碍物(仙人掌群/翼龙)的生成、滚动与碰撞检测。
// 生成规则贴近原版 dino3d(js/src/enemy_manager.js):间隔随速度拉开、
// 仙人掌有 25% 概率"带尾巴"(2~3 株成组)、翼龙在速度档 >=1 后出现。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "sprites.h"

#define OBSTACLE_POOL 8
#define OBS_HEART_BASE_W 22
#define OBS_HEART_BASE_H 20
#define OBS_HEART_MAX_W  24
#define OBS_HEART_MAX_H  22

typedef enum { OBS_CACTUS, OBS_PTERO, OBS_HEART } obs_type_t;

typedef struct {
    bool active;
    obs_type_t type;
    float x, y;          // 锚点: 仙人掌=底部中心, 翼龙=左上角
    const sprite_t *spr; // 当前帧精灵
    const sprite_t *spr2;// 翼龙第二帧(扑翼)
    float anim_timer;
    int anim_frame;
} obstacle_t;

void obstacles_init(int ground_y);
void obstacles_reset(void);

// 每帧更新:滚动、生成、动画、回收。speed_px 为当前地面速度(px/s)。
// lives 用于红心生成门槛(满心不出红心)。
void obstacles_update(float dt, float speed_px, uint32_t score, int speed_level, int lives);

// 把所有活动障碍提交给渲染器。
void obstacles_draw(void);

// 与玩家碰撞盒做 AABB 检测, 返回撞到的障碍物下标, 未撞到返回 -1。
int obstacles_collide(int px, int py, int pw, int ph);

// 取障碍物类型。
obs_type_t obstacles_type(int idx);

// 返回障碍物当前可见中心点；拾取特效用该位置固定在屏幕上。
void obstacles_visual_center(int idx, int *x, int *y);

// 移除障碍物(吃掉红心/撞碎仙人掌时用)。
void obstacles_remove(int idx);
