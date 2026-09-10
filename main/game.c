// main/game.c
// 游戏总控实现。
//
// 数值对齐原版 dino3d:
// - 分数: 10 分/秒, 每 100 分闪烁提示一次(score_manager.js: add_vel=10, step=100)
// - 速度: 四档, 初始 120px/s, 每 100 分升一点, 上限约 3 倍(enemy_manager.js: vel 上限 35)
// - 昼夜: 每 700 分切换一次(原版 chrome dino 的周期), 用调色板整体切换模拟
#include "game.h"
#include "render.h"
#include "player.h"
#include "obstacles.h"
#include "scenery.h"
#include "hud.h"
#include "input.h"
#include "sfx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bsp_display.h"
#include <stdio.h>

// ---- 布局(320x240 横屏, 2D 版 dino3d 沙漠场景) ----
#define GROUND_Y      205   // 恐龙脚底(跑道面)
#define DINO_X         40   // 恐龙水平位置
#define FAR_TOP        86   // 远场景带起始(树/远景所在)
#define RIVER_Y       150   // 河面起始
#define FIELD_Y       174   // 近处跑道起始

// ---- 速度曲线 ----
#define SPEED_BASE    180.0f // px/s(起步更快, 节奏对齐原版)
#define SPEED_STEP      8.0f // 每 100 分增加的 px/s
#define SPEED_MAX     450.0f
#define SCORE_PER_STEP  100  // 每多少分提速/闪一次
#define SCORE_RATE     10.0f // 分/秒
#define DAY_NIGHT_EVERY 700  // 每多少分切换昼夜
#define MAX_LIVES       3    // 红心数
#define INVINCIBLE_MS   1500 // 撞心碎裂后的无敌时间

// ---- 颜色(对齐 dino3d 沙漠色调) ----
#define SKY_DAY       RGB565(238, 203, 110)  // 沙漠黄(原作天空与沙同色)
#define FAR_DAY       RGB565(214, 178, 90)   // 远处河岸
#define RIVER_DAY     RGB565(127, 196, 214)  // 河蓝
#define WAVE_DAY      RGB565(160, 220, 230)  // 波光
#define GROUND_DAY    RGB565(232, 192, 100)  // 跑道沙地
#define SKY_NIGHT     RGB565(16, 16, 48)
#define FAR_NIGHT     RGB565(20, 20, 52)
#define RIVER_NIGHT   RGB565(24, 40, 80)
#define WAVE_NIGHT    RGB565(40, 60, 110)
#define GROUND_NIGHT  RGB565(24, 24, 56)
#define CLOUD_DAY     RGB565(255, 250, 235)
#define CLOUD_NIGHT   RGB565(80, 80, 110)
#define INK_DAY       RGB565(60, 50, 30)
#define INK_NIGHT     RGB565(200, 200, 220)

typedef enum {
    ST_READY,      // 待机: 恐龙站立, 按 UP 开跑
    ST_RUNNING,
    ST_PAUSED,
    ST_GAME_OVER,
    ST_SETTINGS,   // 设置菜单(长按 OK 进入/退出)
} game_state_t;

static player_t s_player;
static game_state_t s_state;
static float s_score;
static uint32_t s_hi_score;
static float s_speed;
static bool s_night;
static uint32_t s_last_flash; // 已提示到的整百
static int64_t s_flash_until; // 闪烁截止时间(esp_timer_get_time)
static int s_lives;           // 剩余红心
static int64_t s_invincible_until; // 无敌截止时间(esp_timer_get_time)

// ---- 设置菜单 ----
static const int VOL_LEVELS[] = { 0, 20, 40, 60, 80, 100 };
static const int BL_LEVELS[]  = { 20, 40, 60, 80, 100 };
#define VOL_COUNT 6
#define BL_COUNT  5
static int s_vol_idx = 3;   // 默认 60%
static int s_bl_idx = 3;    // 默认 80%
static int s_menu_row;      // 0=音量 1=背光
static bool s_editing;      // 是否在某一项的配置态(Up/Down 调值)
static game_state_t s_return_state; // 退出设置后回到哪里

// ---- 最高分持久化(NVS) ----
static uint32_t hi_score_load(void)
{
    nvs_handle_t h;
    uint32_t v = 0;
    if (nvs_open("dino", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "hi", &v);
        nvs_close(h);
    }
    return v;
}

static void hi_score_save(uint32_t v)
{
    nvs_handle_t h;
    if (nvs_open("dino", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "hi", v);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ---- 设置持久化(NVS) ----
static void settings_load(void)
{
    nvs_handle_t h;
    uint32_t v;
    if (nvs_open("dino", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u32(h, "vol", &v) == ESP_OK && v < VOL_COUNT) s_vol_idx = (int)v;
        if (nvs_get_u32(h, "bl", &v) == ESP_OK && v < BL_COUNT) s_bl_idx = (int)v;
        nvs_close(h);
    }
}

static void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("dino", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "vol", (uint32_t)s_vol_idx);
        nvs_set_u32(h, "bl", (uint32_t)s_bl_idx);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void settings_apply(void)
{
    sfx_set_volume((uint8_t)VOL_LEVELS[s_vol_idx]);
    bsp_display_backlight((uint8_t)BL_LEVELS[s_bl_idx]);
}

static int speed_level(void)
{
    // 0..3 四档(原版: vel <10 / 10..20 / 20..30 / >=30)
    float ratio = (s_speed - SPEED_BASE) / (SPEED_MAX - SPEED_BASE);
    int lv = (int)(ratio * 4);
    return lv > 3 ? 3 : (lv < 0 ? 0 : lv);
}

static void game_reset(void)
{
    player_reset(&s_player);
    obstacles_reset();
    scenery_reset();
    s_score = 0;
    s_speed = SPEED_BASE;
    s_night = false;
    s_last_flash = 0;
    s_flash_until = 0;
    s_lives = MAX_LIVES;
    s_invincible_until = 0;
    render_set_night(false);
}

// 设置菜单绘制: 半透明感的深色面板 + 两行档位, 选中行反色
static void draw_settings(uint16_t ink)
{
    const int px = 60, py = 66, pw = 200, ph = 108;
    uint16_t panel = s_night ? RGB565(10, 10, 30) : RGB565(40, 32, 16);
    uint16_t hi_fg = s_night ? RGB565(10, 10, 30) : RGB565(40, 32, 16);
    // 面板 + 边框
    render_fill_rect(px, py, pw, ph, panel);
    render_fill_rect(px, py, pw, 2, ink);
    render_fill_rect(px, py + ph - 2, pw, 2, ink);
    render_fill_rect(px, py, 2, ph, ink);
    render_fill_rect(px + pw - 2, py, 2, ph, ink);

    hud_text("SETTINGS", px + 14, py + 10, ink);

    char buf[24];
    const char *labels[2] = { "VOLUME", "LIGHT" };
    int vals[2] = { VOL_LEVELS[s_vol_idx], BL_LEVELS[s_bl_idx] };
    for (int i = 0; i < 2; i++) {
        int ry = py + 34 + i * 22;
        // 配置态: 值部分闪烁提示可调
        bool blink = s_editing && i == s_menu_row &&
                     ((esp_timer_get_time() / 300000) & 1);
        snprintf(buf, sizeof(buf), "%s < %d%% >", labels[i], vals[i]);
        if (i == s_menu_row) {
            // 选中行反色
            render_fill_rect(px + 8, ry - 3, pw - 16, 16, ink);
            if (blink) {
                // 闪烁时只画标签, 隐去值
                hud_text(labels[i], px + 14, ry, hi_fg);
            } else {
                hud_text(buf, px + 14, ry, hi_fg);
            }
        } else {
            hud_text(buf, px + 14, ry, ink);
        }
    }
}

static void draw_frame(void)
{
    uint16_t sky = s_night ? SKY_NIGHT : SKY_DAY;
    uint16_t ground = s_night ? GROUND_NIGHT : GROUND_DAY;
    uint16_t ink = s_night ? INK_NIGHT : INK_DAY;

    // 背景: FAR_TOP 以下全是地面色, 远场带/河面覆盖上去
    render_begin(sky, ground, FAR_TOP);
    scenery_draw_sky(s_night ? CLOUD_NIGHT : CLOUD_DAY);

    // 远场带(树站立的河岸)
    render_fill_rect(0, FAR_TOP, RENDER_SCREEN_W, RIVER_Y - FAR_TOP,
                     s_night ? FAR_NIGHT : FAR_DAY);
    scenery_draw_far();

    // 伪透视: 河面向右上方收拢(对齐原版地面向远方消失点的观感)
    {
        uint16_t riv_c = s_night ? RIVER_NIGHT : RIVER_DAY;
        uint16_t hor_c = s_night ? RGB565(120, 120, 160) : RGB565(200, 165, 85);
        uint16_t wave = s_night ? WAVE_NIGHT : WAVE_DAY;
        int wave_off = (int)(esp_timer_get_time() / 30000) % 48;
        for (int x = 0; x < RENDER_SCREEN_W; x += 4) {
            int riv_top = RIVER_Y + x / 53;     // 150 → 156
            int riv_bot = FIELD_Y - x / 80;     // 174 → 170 河面变窄
            render_fill_rect(x, riv_top, 4, riv_bot - riv_top, riv_c);
            render_fill_rect(x, riv_bot, 4, 2, hor_c);
        }
        // 波光顺坡流动
        for (int i = 0; i < 8; i++) {
            int x = ((i * 48) - wave_off) % (RENDER_SCREEN_W + 24);
            if (x < 0) x += RENDER_SCREEN_W + 24;
            render_fill_rect(x, RIVER_Y + 3 + (i * 7) % 8 + x / 53, 12, 2, wave);
        }
    }

    scenery_draw_ground();

    obstacles_draw();

    int dx, dy;
    player_draw_pos(&s_player, &dx, &dy);
    // 恐龙脚下的椭圆阴影(离地越高阴影越小越淡)
    {
        const sprite_t *sp = player_sprite(&s_player);
        int air = GROUND_Y - (int)s_player.y;
        int sh_w = sp->w * 4 / 5 - air / 3;
        if (sh_w < 8) sh_w = 8;
        render_fill_rect((int)s_player.x - sh_w / 2 + 4, GROUND_Y + 1,
                         sh_w, 3, RGB565(120, 95, 45));
    }
    // 无敌期间恐龙闪烁(隔 100ms 隐去)
    bool blink_out = esp_timer_get_time() < s_invincible_until &&
                     ((esp_timer_get_time() / 100000) & 1);
    if (!blink_out)
        render_sprite(player_sprite(&s_player), dx, dy);

    // 左上角红心(剩余生命)
    for (int i = 0; i < s_lives; i++)
        render_sprite(&spr_heart, 6 + i * 15, 6);

    // HUD: 破整百时闪烁当前分
    bool blink = esp_timer_get_time() < s_flash_until &&
                 (((uint32_t)(esp_timer_get_time() / 200000)) & 1);
    hud_draw_scores((uint32_t)s_score, s_hi_score, !blink, ink);

    if (s_state == ST_PAUSED) hud_draw_paused(ink);
    if (s_state == ST_GAME_OVER) hud_draw_game_over(ink);
    if (s_state == ST_SETTINGS) draw_settings(ink);

    render_flush();
}

void game_run(void)
{
    render_init();
    input_init();
    sfx_init();
    settings_load();
    settings_apply(); // 音量 + 背光按上次设置生效

    s_hi_score = hi_score_load();
    player_init(&s_player, DINO_X, GROUND_Y);
    obstacles_init(GROUND_Y);
    scenery_init(GROUND_Y, FAR_TOP, RIVER_Y);
    game_reset();
    s_state = ST_READY;

    int64_t last = esp_timer_get_time();

    while (1) {
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1000000.0f;
        last = now;
        if (dt > 0.1f) dt = 0.1f; // 防卡顿大步长穿墙

        game_key_t press = input_take_press();
        bool up_held = input_up_held();
        bool down_held = input_down_held();

        // 长按 OK: 进入/退出设置菜单(任何状态下可用)
        if (input_take_long() == KEY_OK) {
            if (s_state == ST_SETTINGS) {
                settings_save();
                s_state = s_return_state;
            } else {
                // 运行中进入设置等价于先暂停; 退出后回到暂停
                s_return_state = (s_state == ST_RUNNING) ? ST_PAUSED : s_state;
                s_menu_row = 0;
                s_editing = false;
                s_state = ST_SETTINGS;
            }
            draw_frame();
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        switch (s_state) {
        case ST_READY:
            if (press == KEY_UP) { s_state = ST_RUNNING; sfx_play(SFX_JUMP); player_jump(&s_player); }
            player_update(&s_player, dt, false, false, 0);
            break;

        case ST_RUNNING:
            if (press == KEY_OK) { s_state = ST_PAUSED; break; }
            if (press == KEY_UP) { player_jump(&s_player); sfx_play(SFX_JUMP); }

            // 分数与速度
            s_score += SCORE_RATE * dt;
            s_speed = SPEED_BASE + ((uint32_t)s_score / SCORE_PER_STEP) * SPEED_STEP;
            if (s_speed > SPEED_MAX) s_speed = SPEED_MAX;

            // 整百闪烁提示: 跨过整百后闪 ~1 秒
            {
                uint32_t step = (uint32_t)s_score / SCORE_PER_STEP;
                if (step > s_last_flash) {
                    s_last_flash = step;
                    s_flash_until = esp_timer_get_time() + 1000000;
                }
            }

            // 昼夜切换
            {
                bool want_night = (((uint32_t)s_score / DAY_NIGHT_EVERY) & 1) == 1;
                if (want_night != s_night) {
                    s_night = want_night;
                    render_set_night(s_night);
                }
            }

            player_update(&s_player, dt, up_held, down_held, speed_level());
            obstacles_update(dt, s_speed, (uint32_t)s_score, speed_level(), s_lives);
            scenery_update(dt, s_speed);

            // 碰撞: 红心=吃掉, 障碍=扣心(心碎裂消失 + 无敌 1.5s)
            {
                int hx, hy, hw, hh;
                player_hitbox(&s_player, &hx, &hy, &hw, &hh);
                int hit = obstacles_collide(hx, hy, hw, hh);
                if (hit >= 0) {
                    if (obstacles_type(hit) == OBS_HEART) {
                        obstacles_remove(hit);
                        if (s_lives < MAX_LIVES) s_lives++;
                        sfx_play(SFX_JUMP);
                    } else if (esp_timer_get_time() > s_invincible_until) {
                        obstacles_remove(hit);
                        s_lives--;
                        if (s_lives <= 0) {
                            s_player.dead = true;
                            s_state = ST_GAME_OVER;
                            sfx_play(SFX_DEATH);
                            if ((uint32_t)s_score > s_hi_score) {
                                s_hi_score = (uint32_t)s_score;
                                hi_score_save(s_hi_score);
                            }
                        } else {
                            s_invincible_until = esp_timer_get_time() + INVINCIBLE_MS * 1000;
                            sfx_play(SFX_DEATH);
                        }
                    }
                }
            }
            break;

        case ST_PAUSED:
            if (press == KEY_OK) s_state = ST_RUNNING;
            // 冻结: 不更新任何逻辑, 画面原样重绘
            break;

        case ST_GAME_OVER:
            // UP/DOWN/OK 任意键重开(Power 键独立于 ADC, 不会产生事件)
            if (press != KEY_NONE) {
                game_reset();
                s_state = ST_RUNNING;
            }
            break;

        case ST_SETTINGS:
            // 列表态: UP/DOWN 选行, OK 单击进入该项配置
            // 配置态: UP 调高 / DOWN 调低, OK 单击退出配置
            // (此状态下所有按键都被这里消费, 不会触发跳跃/下蹲/暂停/音效)
            if (!s_editing) {
                if (press == KEY_UP || press == KEY_DOWN)
                    s_menu_row ^= 1;
                if (input_take_click() == KEY_OK)
                    s_editing = true;
            } else {
                int dir = (press == KEY_UP) ? 1 : (press == KEY_DOWN) ? -1 : 0;
                if (dir != 0) {
                    if (s_menu_row == 0) {
                        s_vol_idx += dir;
                        if (s_vol_idx < 0) s_vol_idx = 0;
                        if (s_vol_idx >= VOL_COUNT) s_vol_idx = VOL_COUNT - 1;
                    } else {
                        s_bl_idx += dir;
                        if (s_bl_idx < 0) s_bl_idx = 0;
                        if (s_bl_idx >= BL_COUNT) s_bl_idx = BL_COUNT - 1;
                    }
                    settings_apply(); // 立即生效(音量/背光)
                }
                if (input_take_click() == KEY_OK)
                    s_editing = false;
            }
            break;
        }

        draw_frame();
        vTaskDelay(pdMS_TO_TICKS(5)); // 限制在 ~60fps 以内, 让出 CPU
    }
}
