// main/scenery.h
// 场景装饰:云朵(视差)、地面纹理斑点、地面小装饰(石头/花/骷髅/蝎子/风滚草)。
// 均为纯视觉元素,不参与碰撞。
#pragma once

#include <stdint.h>
#include <stdbool.h>

void scenery_init(int ground_y, int far_top, int far_bottom);
void scenery_reset(void);

// 每帧更新。speed_px 为地面速度; 暂停/结束时传 0 即冻结。
void scenery_update(float dt, float speed_px);

// 天空层(星星 + 云)。night_progress 0..1；game_time_s 暂停时不推进。
void scenery_draw_sky(uint16_t cloud_color, uint16_t star_dim,
                      uint16_t star_bright, float night_progress,
                      float game_time_s);

// 远景层(树/小仙人掌, 远河岸上, 半速视差)。
void scenery_draw_far(void);

// 地面层(斑点 + 近处装饰)。
void scenery_draw_ground(uint16_t speckle_color);
