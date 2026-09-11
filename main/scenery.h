// main/scenery.h
// 场景装饰:云朵、天空/中景、沙漠植物、石头、蝎子和风滚草。
// 均为纯视觉元素,不参与碰撞。
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "scene.h"

void scenery_init(int ground_y, int mid_top, int mid_bottom, int field_y);
void scenery_reset(void);
void scenery_set_scene(scene_id_t current);

// 每帧更新。speed_px 为地面速度; 暂停/结束时传 0 即冻结。
void scenery_update(float dt, float speed_px);

// 场景专属背景的中景滚动偏移，供场景地标绘制使用。
float scenery_scene_mid_scroll(void);

// 天空低频节拍到达时推进云层。scroll_px 为自上次天空刷新起累计的地面位移。
void scenery_update_sky(float scroll_px);

// 当前可见中景物体的最顶部；没有跨入屏幕的物体时返回屏幕高度。
int scenery_dynamic_top(void);

// 天空层(星星 + 云 + 16x16 离线烘焙太阳/月亮)。
// celestial_progress 为当前白天/夜晚分段进度：太阳右→左，月亮左→右。
void scenery_draw_sky(uint16_t cloud_color, uint16_t star_dim,
                      uint16_t star_bright, float night_progress,
                      bool is_night, float celestial_progress,
                      float game_time_s);

// 中景后层(草/龙舌兰等低细节装饰)。
void scenery_draw_mid_back(void);

// 地面后层：固定地面装饰；仅风滚草保留独立滚动/旋转/弹跳。
void scenery_draw_ground_back(uint16_t speckle_color);

// 地面前层：保留兼容接口，当前不再绘制遮挡层。
void scenery_draw_ground_front(void);
