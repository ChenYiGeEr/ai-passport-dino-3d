#include "idle_sleep.h"

void idle_sleep_init(idle_sleep_t *idle, int64_t now_us, bool eligible)
{
    *idle = (idle_sleep_t) {
        .deadline_us = now_us + IDLE_SLEEP_TIMEOUT_US,
        .release_since_us = -1,
        .eligible = eligible,
    };
}

void idle_sleep_reset(idle_sleep_t *idle, int64_t now_us)
{
    idle->deadline_us = now_us + IDLE_SLEEP_TIMEOUT_US;
}

idle_sleep_event_t idle_sleep_update(idle_sleep_t *idle, int64_t now_us,
                                     bool eligible, bool key_pressed,
                                     bool key_held)
{
    if (eligible != idle->eligible) {
        bool was_asleep = idle->asleep;
        idle->eligible = eligible;
        idle->asleep = false;
        idle->suppressing_gesture = false;
        idle->release_since_us = -1;
        idle_sleep_reset(idle, now_us);
        return was_asleep ? IDLE_SLEEP_EVENT_WAKE : IDLE_SLEEP_EVENT_NONE;
    }

    if (!eligible)
        return IDLE_SLEEP_EVENT_NONE;

    if (idle->asleep) {
        if (!key_pressed)
            return IDLE_SLEEP_EVENT_NONE;
        idle->asleep = false;
        idle->suppressing_gesture = true;
        idle->release_since_us = -1;
        idle_sleep_reset(idle, now_us);
        return IDLE_SLEEP_EVENT_WAKE;
    }

    if (idle->suppressing_gesture) {
        if (key_held) {
            idle->release_since_us = -1;
        } else if (idle->release_since_us < 0) {
            idle->release_since_us = now_us;
        } else if (now_us - idle->release_since_us >= IDLE_SLEEP_RELEASE_SETTLE_US) {
            idle->suppressing_gesture = false;
            idle_sleep_reset(idle, now_us);
        }
        return IDLE_SLEEP_EVENT_NONE;
    }

    if (key_pressed)
        idle_sleep_reset(idle, now_us);
    if (now_us >= idle->deadline_us) {
        idle->asleep = true;
        return IDLE_SLEEP_EVENT_SLEEP;
    }
    return IDLE_SLEEP_EVENT_NONE;
}

bool idle_sleep_is_asleep(const idle_sleep_t *idle)
{
    return idle->asleep;
}

bool idle_sleep_consumes_input(const idle_sleep_t *idle)
{
    return idle->asleep || idle->suppressing_gesture;
}
