// 非运行状态的屏幕空闲计时与唤醒手势状态机。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IDLE_SLEEP_TIMEOUT_US        (30LL * 1000 * 1000)
#define IDLE_SLEEP_RELEASE_SETTLE_US (50LL * 1000)

typedef enum {
    IDLE_SLEEP_EVENT_NONE = 0,
    IDLE_SLEEP_EVENT_SLEEP,
    IDLE_SLEEP_EVENT_WAKE,
} idle_sleep_event_t;

typedef struct {
    int64_t deadline_us;
    int64_t release_since_us;
    bool eligible;
    bool asleep;
    bool suppressing_gesture;
} idle_sleep_t;

void idle_sleep_init(idle_sleep_t *idle, int64_t now_us, bool eligible);
void idle_sleep_reset(idle_sleep_t *idle, int64_t now_us);

// key_pressed 是按下边沿，key_held 是当前 ADC 仍检测到任意键。
idle_sleep_event_t idle_sleep_update(idle_sleep_t *idle, int64_t now_us,
                                     bool eligible, bool key_pressed,
                                     bool key_held);

bool idle_sleep_is_asleep(const idle_sleep_t *idle);
bool idle_sleep_consumes_input(const idle_sleep_t *idle);
