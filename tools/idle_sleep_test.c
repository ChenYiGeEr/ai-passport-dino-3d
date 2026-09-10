// 宿主机逻辑测试：非运行状态30秒息屏及完整唤醒手势吞键。
// 用法: cc -std=c11 -I main tools/idle_sleep_test.c main/idle_sleep.c -o /tmp/idle_sleep_test
#include <assert.h>
#include <stdio.h>

#include "../main/idle_sleep.h"

int main(void)
{
    idle_sleep_t idle;
    idle_sleep_init(&idle, 0, true);

    assert(idle_sleep_update(&idle, IDLE_SLEEP_TIMEOUT_US - 1,
                             true, false, false) == IDLE_SLEEP_EVENT_NONE);
    assert(idle_sleep_update(&idle, IDLE_SLEEP_TIMEOUT_US,
                             true, false, false) == IDLE_SLEEP_EVENT_SLEEP);
    assert(idle_sleep_is_asleep(&idle));

    assert(idle_sleep_update(&idle, IDLE_SLEEP_TIMEOUT_US + 1000,
                             true, true, true) == IDLE_SLEEP_EVENT_WAKE);
    assert(!idle_sleep_is_asleep(&idle));
    assert(idle_sleep_consumes_input(&idle));

    // 松开不足50ms，CLICK/LONG仍应被上层吞掉。
    assert(idle_sleep_update(&idle, IDLE_SLEEP_TIMEOUT_US + 2000,
                             true, false, false) == IDLE_SLEEP_EVENT_NONE);
    assert(idle_sleep_consumes_input(&idle));
    assert(idle_sleep_update(&idle, IDLE_SLEEP_TIMEOUT_US + 51000,
                             true, false, false) == IDLE_SLEEP_EVENT_NONE);
    assert(idle_sleep_consumes_input(&idle));
    assert(idle_sleep_update(&idle, IDLE_SLEEP_TIMEOUT_US + 52000,
                             true, false, false) == IDLE_SLEEP_EVENT_NONE);
    assert(!idle_sleep_consumes_input(&idle));

    // 运行态永不息屏；重新进入非运行态时重新计完整30秒。
    assert(idle_sleep_update(&idle, 100000000, false, false, false) ==
           IDLE_SLEEP_EVENT_NONE);
    assert(idle_sleep_update(&idle, 200000000, false, false, false) ==
           IDLE_SLEEP_EVENT_NONE);
    assert(idle_sleep_update(&idle, 200000001, true, false, false) ==
           IDLE_SLEEP_EVENT_NONE);
    assert(idle_sleep_update(&idle, 200000001 + IDLE_SLEEP_TIMEOUT_US,
                             true, false, false) == IDLE_SLEEP_EVENT_SLEEP);

    puts("idle_sleep_test: ok");
    return 0;
}
