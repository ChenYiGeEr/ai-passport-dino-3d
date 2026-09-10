// 四阶段昼夜调色板的时间推进与 RGB565 插值。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DAY_CYCLE_STAGE_COUNT 4
#define DAY_CYCLE_LAST_STAGE  3.0f
#define DAY_CYCLE_DURATION_S  2.0f

typedef struct {
    float phase; // 0=白天, 1=落日, 2=暮色, 3=夜晚
} day_cycle_t;

void day_cycle_reset(day_cycle_t *cycle);
void day_cycle_update(day_cycle_t *cycle, bool want_night, float dt);

// 在四个关键色之间按当前阶段连续插值。
uint16_t day_cycle_color(const day_cycle_t *cycle,
                         const uint16_t colors[DAY_CYCLE_STAGE_COUNT]);

// 供精灵调色与星星显隐使用。
uint8_t day_cycle_night_mix(const day_cycle_t *cycle);
float day_cycle_night_progress(const day_cycle_t *cycle);
