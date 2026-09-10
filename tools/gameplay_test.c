// 宿主机逻辑测试：跳跃缓冲、落地事件和昼夜四阶段过渡。
// 用法: cc -std=c11 -I main tools/gameplay_test.c main/player.c main/day_cycle.c main/sprites.c -lm -o /tmp/gameplay_test
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../main/day_cycle.h"
#include "../main/player.h"

static void test_jump_buffer(void)
{
    player_t p;
    player_init(&p, 40, 205);

    player_queue_jump(&p);
    player_event_t event = player_update(&p, 0.01f, true, false, 0);
    assert(event & PLAYER_EVENT_JUMPED);
    assert(!p.on_ground);

    // 落地前约 60ms 按下：落地帧同时报告 landed+jumped。
    player_reset(&p);
    p.on_ground = false;
    p.y = 202.0f;
    p.vel_y = -20.0f;
    player_queue_jump(&p);
    event = player_update(&p, 0.06f, false, false, 0);
    assert((event & (PLAYER_EVENT_LANDED | PLAYER_EVENT_JUMPED)) ==
           (PLAYER_EVENT_LANDED | PLAYER_EVENT_JUMPED));
    assert(!p.on_ground);

    // 超过 80ms 后才落地：输入已过期，只报告 landed。
    player_reset(&p);
    p.on_ground = false;
    p.y = 100.0f;
    p.vel_y = 0;
    player_queue_jump(&p);
    event = player_update(&p, 0.081f, false, false, 0);
    assert(event == PLAYER_EVENT_NONE);
    p.y = 204.0f;
    p.vel_y = -20.0f;
    event = player_update(&p, 0.03f, false, false, 0);
    assert(event == PLAYER_EVENT_LANDED);
    assert(p.on_ground);
}

static void test_day_cycle(void)
{
    day_cycle_t cycle;
    const uint16_t colors[4] = { 0x0000, 0x1111, 0x2222, 0x3333 };
    day_cycle_reset(&cycle);
    assert(day_cycle_color(&cycle, colors) == colors[0]);

    day_cycle_update(&cycle, true, 2.0f / 3.0f);
    assert(fabsf(cycle.phase - 1.0f) < 0.001f);
    assert(day_cycle_color(&cycle, colors) == colors[1]);
    day_cycle_update(&cycle, true, 2.0f / 3.0f);
    assert(fabsf(cycle.phase - 2.0f) < 0.001f);
    assert(day_cycle_color(&cycle, colors) == colors[2]);
    day_cycle_update(&cycle, true, 2.0f / 3.0f);
    assert(cycle.phase == 3.0f);
    assert(day_cycle_color(&cycle, colors) == colors[3]);
    assert(day_cycle_night_mix(&cycle) == 255);

    // 暂停传入 dt=0，不推进；反向也恰好在 2 秒内回到白天。
    day_cycle_update(&cycle, false, 0);
    assert(cycle.phase == 3.0f);
    day_cycle_update(&cycle, false, 2.0f);
    assert(cycle.phase == 0);
}

int main(void)
{
    test_jump_buffer();
    test_day_cycle();
    puts("gameplay_test: ok");
    return 0;
}
