// 恐龙起跑与落地时的短促扬尘粒子。
#pragma once

#include <stdint.h>

void dust_reset(void);
void dust_emit_start(int foot_x, int foot_y);
void dust_emit_land(int foot_x, int foot_y);
void dust_update(float dt);
void dust_draw(uint16_t near_color, uint16_t far_color);
