// main/input.h
// 按键输入:ADC 三键(UP/DOWN/OK)的按下事件 + 按住状态轮询。
//
// 硬件事实(见 ai-passport BSP):
// - 三键共用 GPIO0 ADC, BSP 只有"按下"事件, 无"松开"事件;
//   按住状态用 bsp_button_read_mv() 每帧轮询电压窗口。
// - Power 键独立于 ADC 三键, 不产生任何按键事件, 天然不会触发重开。
//
// 冲突处理:
// - DOWN 按住时忽略 UP(下蹲中禁止起跳, 原版同样如此);
// - 跳跃为边沿触发, 每次按下只触发一次, 长按不连跳;
// - OK 短按暂停/恢复, 长按不做事(独立固件没有菜单可回)。
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_OK,
} game_key_t;

void input_init(void);

// 取一次性的"按下"事件(边沿触发, 取走即清)。没有则返回 KEY_NONE。
game_key_t input_take_press(void);

// 取一次性的"单击"事件(快速按下并松开才算; 长按不会触发)。仅 OK 键。
game_key_t input_take_click(void);

// 取一次性的"长按"事件。仅 OK 键。
game_key_t input_take_long(void);

// 当前按住状态(供下蹲/长按跳跃加高用)。
bool input_up_held(void);
bool input_down_held(void);
bool input_ok_held(void);

// 当前按住的键；三键共用 ADC，同一时刻最多识别一个键。
game_key_t input_held_key(void);

// 丢弃尚未消费的 PRESS/CLICK/LONG，供息屏唤醒吞掉完整手势。
void input_discard_events(void);
