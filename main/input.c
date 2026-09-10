// main/input.c
#include "input.h"
#include "bsp_button.h"

#include <stdatomic.h>

// 按下事件队列(深度 1 足够: 游戏只关心最近一次按下)
static atomic_int s_press = KEY_NONE;
static atomic_int s_click = KEY_NONE;
static atomic_int s_long  = KEY_NONE;

// 电压窗口(与 bsp_pins.h 的 BSP_BTN_MV_TABLE 一致):
// UP 0~150, DOWN 150~447, OK 447~1900, 松开 ~3300。
static game_key_t key_from_mv(int mv)
{
    if (mv < 0) return KEY_NONE;        // 读取失败
    if (mv < 150) return KEY_UP;
    if (mv < 447) return KEY_DOWN;
    if (mv < 1900) return KEY_OK;
    return KEY_NONE;
}

static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    game_key_t k = KEY_NONE;
    switch (btn) {
    case BSP_BTN_UP:   k = KEY_UP;   break;
    case BSP_BTN_DOWN: k = KEY_DOWN; break;
    case BSP_BTN_OK:   k = KEY_OK;   break;
    }
    // PRESS 全部转发(低延迟, 跳跃用);
    // CLICK/LONG 只转发 OK(设置菜单用), 避免 UP/DOWN 的抬起事件干扰。
    if (ev == BSP_BTN_PRESS)
        atomic_store(&s_press, (int)k);
    else if (ev == BSP_BTN_CLICK && k == KEY_OK)
        atomic_store(&s_click, (int)k);
    else if (ev == BSP_BTN_LONG && k == KEY_OK)
        atomic_store(&s_long, (int)k);
}

void input_init(void)
{
    bsp_button_init(on_button, NULL);
}

game_key_t input_take_press(void)
{
    return (game_key_t)atomic_exchange(&s_press, KEY_NONE);
}

game_key_t input_take_click(void)
{
    return (game_key_t)atomic_exchange(&s_click, KEY_NONE);
}

game_key_t input_take_long(void)
{
    return (game_key_t)atomic_exchange(&s_long, KEY_NONE);
}

bool input_up_held(void)   { return key_from_mv(bsp_button_read_mv()) == KEY_UP; }
bool input_down_held(void) { return key_from_mv(bsp_button_read_mv()) == KEY_DOWN; }
bool input_ok_held(void)   { return key_from_mv(bsp_button_read_mv()) == KEY_OK; }

game_key_t input_held_key(void)
{
    return key_from_mv(bsp_button_read_mv());
}

void input_discard_events(void)
{
    atomic_store(&s_press, KEY_NONE);
    atomic_store(&s_click, KEY_NONE);
    atomic_store(&s_long, KEY_NONE);
}
