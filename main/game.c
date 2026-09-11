// main/game.c
// 游戏总控实现。
//
// 数值对齐原版 dino3d:
// - 分数: 10 分/秒, 每 100 分闪烁提示一次(score_manager.js: add_vel=10, step=100)
// - 速度: 四档, 初始 120px/s, 每 100 分升一点, 上限约 3 倍(enemy_manager.js: vel 上限 35)
// - 昼夜: 每 700 分切换一次，2 秒内经过四个关键色平滑过渡
#include "game.h"
#include "render.h"
#include "player.h"
#include "obstacles.h"
#include "scenery.h"
#include "hud.h"
#include "input.h"
#include "sfx.h"
#include "fap_screenshot.h"
#include "day_cycle.h"
#include "dust.h"
#include "idle_sleep.h"
#include "scene.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bsp_display.h"
#include <stdio.h>

static const char *TAG = "game";

// ---- 布局(320x240 横屏, 2D 版 dino3d 沙漠场景) ----
#define GROUND_Y      205   // 恐龙脚底(跑道面)
#define DINO_X         40   // 恐龙水平位置
#define FAR_TOP        86   // 远场景带起始(树/远景所在)
#define RIVER_Y       150   // 河面起始
#define FIELD_Y       174   // 近处跑道起始

// ---- 速度曲线 ----
#define SPEED_BASE    180.0f // px/s(起步更快, 节奏对齐原版)
#define SPEED_STEP      8.0f // 每 100 分增加的 px/s
#define SPEED_MAX     300.0f // 调试与可玩性上限，避免障碍进入过快
#define SCORE_PER_STEP  100  // 每多少分提速/闪一次
#define SCORE_RATE     10.0f // 分/秒
#define DAY_SCORE_SPAN 700   // 白天持续分数
#define NIGHT_SCORE_SPAN 500  // 夜晚持续分数
#define MAX_LIVES       3    // 红心数
#define INVINCIBLE_MS   1500 // 撞心碎裂后的无敌时间
#define HEART_FX_MS      450 // 拾取位置放大淡出与 HUD 弹跳时长

// ---- 刷新调度 ----
#define RUN_FRAME_PERIOD_US  16667 // 60 FPS，绝对截止时间调度
#define SKY_REFRESH_US      100000 // 天空/HUD 至少每 100ms 整屏更新
#define STATIC_POLL_MS          20 // 静止页面只轮询输入，不持续推屏
#define SETTINGS_FADE_US    150000 // 进入/退出设置 150ms
#define SETTINGS_FADE_STEPS      5 // 量化为 5 档，约 30 FPS
#define SETTINGS_DIM_RETAIN    128 // 完全展开时背景保留约 50% 亮度

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

#define SKY_SUNSET       RGB565(224, 142, 82)
#define SKY_DUSK         RGB565(91, 65, 96)
#define FAR_SUNSET       RGB565(190, 112, 68)
#define FAR_DUSK         RGB565(76, 54, 82)
#define RIVER_SUNSET     RGB565(91, 145, 166)
#define RIVER_DUSK       RGB565(48, 71, 112)
#define WAVE_SUNSET      RGB565(138, 187, 191)
#define WAVE_DUSK        RGB565(76, 96, 139)
#define GROUND_SUNSET    RGB565(204, 142, 76)
#define GROUND_DUSK      RGB565(91, 61, 76)
#define CLOUD_SUNSET     RGB565(255, 214, 180)
#define CLOUD_DUSK       RGB565(151, 122, 145)
#define INK_SUNSET       RGB565(67, 43, 28)
#define INK_DUSK         RGB565(170, 154, 170)

#define SETTINGS_PANEL RGB565(239, 202, 126)
#define SETTINGS_INK   RGB565(70, 45, 24)
static const uint16_t INK_COLORS[] = {
    RGB565(60, 50, 30), RGB565(67, 43, 28), RGB565(170, 154, 170), RGB565(200, 200, 220)
};

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
static bool s_want_night;
static float s_cycle_segment_start;
static bool s_scene_switch_pending;
static day_cycle_t s_day_cycle;
static float s_game_time_s;
static uint32_t s_last_flash; // 已提示到的整百
static float s_flash_until;   // 游戏时间秒；暂停时冻结
static int s_lives;           // 剩余红心
static float s_invincible_until; // 游戏时间秒；暂停时冻结
static idle_sleep_t s_idle_sleep;
static scene_manager_t s_scene;
static bool s_death_feedback;
static float s_death_feedback_s;
static uint16_t scene_day_color(int layer);

static uint8_t shadow_opacity(void)
{
    /* 白天清晰；黄昏两秒内淡出；最深夜完全关闭。 */
    float p = s_day_cycle.phase;
    if (p <= 1.0f) return 255;
    if (p >= 2.0f) return 0;
    return (uint8_t)((2.0f - p) * 255.0f + 0.5f);
}
typedef struct { bool active; float x, y, vx, vy, life; } fragment_t;
static fragment_t s_fragments[5];

static void fragments_emit(int x, int y)
{
    static const int8_t VX[5] = {-42, -20, 0, 23, 45};
    static const int8_t VY[5] = {-92, -120, -105, -116, -82};
    for (int i=0;i<5;i++) s_fragments[i]=(fragment_t){true,(float)x,(float)y,VX[i],VY[i],0.35f};
}
static void fragments_update(float dt)
{
    for (int i=0;i<5;i++) if (s_fragments[i].active) {
        fragment_t *f=&s_fragments[i]; f->life-=dt; f->x+=f->vx*dt; f->y+=f->vy*dt; f->vy+=320.0f*dt;
        if (f->life<=0) f->active=false;
    }
}
static void fragments_draw(void)
{
    for (int i=0;i<5;i++) if (s_fragments[i].active)
        render_fill_rect((int)s_fragments[i].x,(int)s_fragments[i].y,3,3,scene_day_color(SCENE_LAYER_HORIZON));
}

static uint16_t color_blend565(uint16_t a, uint16_t b, uint8_t mix)
{
    int ar=(a>>11)&31, ag=(a>>5)&63, ab=a&31;
    int br=(b>>11)&31, bg=(b>>5)&63, bb=b&31;
    return (uint16_t)(((ar+(br-ar)*mix/255)<<11)|((ag+(bg-ag)*mix/255)<<5)|(ab+(bb-ab)*mix/255));
}

static uint16_t color_darken565(uint16_t c, uint8_t percent)
{
    int scale = 100 - percent;
    int r = ((c >> 11) & 31) * scale / 100;
    int g = ((c >> 5) & 63) * scale / 100;
    int b = (c & 31) * scale / 100;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t scene_day_color(int layer)
{
    uint16_t a[4], b[4];
    for (int i=0;i<4;i++) {
        a[i]=scene_color(scene_manager_current(&s_scene),layer,i);
        b[i]=scene_color(scene_manager_next(&s_scene),layer,i);
    }
    uint16_t ca=day_cycle_color(&s_day_cycle,a), cb=day_cycle_color(&s_day_cycle,b);
    float m=scene_manager_mix(&s_scene); return color_blend565(ca,cb,(uint8_t)(m*255.0f+0.5f));
}

static uint16_t scene_day_far_color(void)
{
    uint16_t far = scene_day_color(SCENE_LAYER_FAR);
    // 非沙漠远景是中景的深化版本，避免与天空和中景装饰撞色。
    if (scene_manager_current(&s_scene) != SCENE_DESERT ||
        scene_manager_next(&s_scene) != SCENE_DESERT)
        far = color_darken565(far, 15);
    return far;
}

static void draw_scene_landmarks(scene_id_t scene, uint8_t opacity)
{
    render_set_opacity(opacity);
    const sprite_t *sprites[3] = {0};
    int xs[3] = { 18, 118, 230 };
    int bottoms[3] = { 132, 148, 136 };
    if (scene == SCENE_CANYON) {
        sprites[0] = &spr_canyon_bg_0; sprites[1] = &spr_canyon_bg_1; sprites[2] = &spr_canyon_bg_2;
        xs[0] = 16; xs[1] = 128; xs[2] = 244;
    } else if (scene == SCENE_OASIS) {
        sprites[0] = &spr_oasis_bg_0; sprites[1] = &spr_oasis_bg_1; sprites[2] = &spr_oasis_bg_2;
        xs[0] = 24; xs[1] = 138; xs[2] = 256;
    } else if (scene == SCENE_VOLCANO) {
        sprites[0] = &spr_volcano_bg_0; sprites[1] = &spr_volcano_bg_1; sprites[2] = &spr_volcano_bg_2;
        xs[0] = 34; xs[1] = 146; xs[2] = 258;
    }
    float far_scroll = scenery_scene_far_scroll();
    float mid_scroll = scenery_scene_mid_scroll();
    for (int i = 0; i < 3; i++) {
        if (!sprites[i]) continue;
        float off = (i == 1) ? mid_scroll : far_scroll;
        int x = xs[i] - (int)off;
        while (x + sprites[i]->w < -4) x += RENDER_SCREEN_W + 40;
        while (x > RENDER_SCREEN_W + 4) x -= RENDER_SCREEN_W + 40;
        render_sprite(sprites[i], x, bottoms[i] - sprites[i]->h);
    }
    render_set_opacity(255);
}

static void draw_gradient_band(int y0, int y1, uint16_t from, uint16_t to)
{
    const int step = 4;
    int span = y1 - y0;
    if (span <= 0) return;
    for (int y = y0; y < y1; y += step) {
        int end = y + step;
        if (end > y1) end = y1;
        int mid = y + (end - y) / 2;
        uint8_t mix = (uint8_t)(((mid - y0) * 255) / span);
        render_fill_rect(0, y, RENDER_SCREEN_W, end - y,
                         color_blend565(from, to, mix));
    }
}

static void draw_background_layers(uint16_t sky, uint16_t far,
                                   uint16_t horizon, uint16_t ground)
{
    // 大块纯色区减少命令数量，天空—远景交界用 24px 渐变消除硬切线。
    render_fill_rect(0, FAR_TOP, RENDER_SCREEN_W, FIELD_Y - FAR_TOP, far);
    render_fill_rect(0, 144, RENDER_SCREEN_W, 14, horizon);
    draw_gradient_band(FAR_TOP - 24, FAR_TOP, sky, far);
    draw_gradient_band(126, 144, far, horizon);
    draw_gradient_band(156, FIELD_Y, horizon, ground);
}

typedef struct {
    bool active;
    float started_at;
    int x, y;
} heart_pickup_fx_t;

typedef struct {
    bool active;
    bool entering;
    int64_t started_at_us;
} settings_fade_t;

static heart_pickup_fx_t s_heart_pickup_fx;
static bool s_hud_heart_pop_active;
static float s_hud_heart_pop_started_at;
static int s_hud_heart_pop_index;
static settings_fade_t s_settings_fade;
static uint8_t s_settings_mix;

typedef struct {
    int64_t window_start_us;
    int64_t slowest_frame_us;
    int64_t frame_total_us;
    int64_t render_total_us;
    int64_t render_max_us;
    int64_t dynamic_start_y_total;
    uint32_t frames;
    uint32_t sky_refreshes;
    uint32_t dynamic_refreshes;
} perf_stats_t;

static perf_stats_t s_perf;

// ---- 设置菜单 ----
static const int VOL_LEVELS[] = { 0, 20, 40, 60, 80, 100 };
static const int BL_LEVELS[]  = { 20, 40, 60, 80, 100 };
#define VOL_COUNT 6
#define BL_COUNT  5
static int s_vol_idx = 3;   // 默认 60%
static int s_bl_idx = 3;    // 默认 80%
static bool s_invincible_mode; // 调试无敌模式，默认关闭
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
        if (nvs_get_u32(h, "inv", &v) == ESP_OK) s_invincible_mode = v != 0;
        nvs_close(h);
    }
}

static void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("dino", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "vol", (uint32_t)s_vol_idx);
        nvs_set_u32(h, "bl", (uint32_t)s_bl_idx);
        nvs_set_u32(h, "inv", s_invincible_mode ? 1u : 0u);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void settings_apply(void)
{
    sfx_set_volume((uint8_t)VOL_LEVELS[s_vol_idx]);
    bsp_display_backlight((uint8_t)BL_LEVELS[s_bl_idx]);
}

static void settings_fade_start(bool entering, int64_t now_us)
{
    s_settings_fade.active = true;
    s_settings_fade.entering = entering;
    s_settings_fade.started_at_us = now_us;
    s_settings_mix = entering ? 0 : 255;
}

// 返回 true 表示亮度档位或页面状态发生变化，需要重绘整屏。
static bool settings_fade_update(int64_t now_us)
{
    if (!s_settings_fade.active) return false;
    int64_t elapsed = now_us - s_settings_fade.started_at_us;
    int step = elapsed >= SETTINGS_FADE_US
        ? SETTINGS_FADE_STEPS - 1
        : (int)(elapsed * (SETTINGS_FADE_STEPS - 1) / SETTINGS_FADE_US);
    uint8_t target = (uint8_t)((step * 255 + (SETTINGS_FADE_STEPS - 1) / 2) /
                               (SETTINGS_FADE_STEPS - 1));
    if (!s_settings_fade.entering) target = 255 - target;
    bool changed = target != s_settings_mix;
    s_settings_mix = target;

    if (elapsed >= SETTINGS_FADE_US) {
        bool entering = s_settings_fade.entering;
        s_settings_fade.active = false;
        if (!entering) s_state = s_return_state;
        changed = true;
    }
    return changed;
}

static bool heart_effects_update(void)
{
    float duration = HEART_FX_MS / 1000.0f;
    bool changed = false;
    if (s_heart_pickup_fx.active &&
        s_game_time_s - s_heart_pickup_fx.started_at >= duration) {
        s_heart_pickup_fx.active = false;
        changed = true;
    }
    if (s_hud_heart_pop_active &&
        s_game_time_s - s_hud_heart_pop_started_at >= duration) {
        s_hud_heart_pop_active = false;
        changed = true;
    }
    return changed;
}

static bool sleep_eligible(game_state_t state)
{
    return state != ST_RUNNING;
}

static void perf_reset(void)
{
    s_perf = (perf_stats_t) { 0 };
}

static void perf_record(int64_t frame_start_us, int64_t render_start_us,
                        int64_t frame_end_us, int refresh_start_y)
{
    if (s_perf.window_start_us == 0)
        s_perf.window_start_us = frame_start_us;
    int64_t frame_us = frame_end_us - frame_start_us;
    int64_t render_us = frame_end_us - render_start_us;
    if (frame_us > s_perf.slowest_frame_us) s_perf.slowest_frame_us = frame_us;
    if (render_us > s_perf.render_max_us) s_perf.render_max_us = render_us;
    s_perf.frame_total_us += frame_us;
    s_perf.render_total_us += render_us;
    s_perf.frames++;
    if (refresh_start_y == 0) {
        s_perf.sky_refreshes++;
    } else {
        s_perf.dynamic_refreshes++;
        s_perf.dynamic_start_y_total += refresh_start_y;
    }

    int64_t elapsed_us = frame_end_us - s_perf.window_start_us;
    if (elapsed_us < 2000000 || s_perf.frames == 0)
        return;
    double dynamic_y_avg = s_perf.dynamic_refreshes
        ? (double)s_perf.dynamic_start_y_total / (double)s_perf.dynamic_refreshes
        : 0.0;
    ESP_LOGI(TAG,
             "fps=%.1f frame_avg=%.1fms slow=%.1fms render_avg=%.1fms "
             "render_max=%.1fms start_y_avg=%.1f sky=%u dynamic=%u spi=%dMHz",
             (double)s_perf.frames * 1000000.0 / (double)elapsed_us,
             (double)s_perf.frame_total_us / (double)s_perf.frames / 1000.0,
             (double)s_perf.slowest_frame_us / 1000.0,
             (double)s_perf.render_total_us / (double)s_perf.frames / 1000.0,
             (double)s_perf.render_max_us / 1000.0,
             dynamic_y_avg, (unsigned)s_perf.sky_refreshes,
             (unsigned)s_perf.dynamic_refreshes,
             bsp_display_pclk_hz() / 1000000);
    perf_reset();
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
    s_want_night = false;
    s_cycle_segment_start = 0;
    s_scene_switch_pending = false;
    day_cycle_reset(&s_day_cycle);
    s_game_time_s = 0;
    s_last_flash = 0;
    s_flash_until = 0;
    s_lives = MAX_LIVES;
    s_invincible_until = 0;
    s_heart_pickup_fx.active = false;
    s_hud_heart_pop_active = false;
    dust_reset();
    render_set_night_mix(0);
    scene_manager_reset(&s_scene);
    scenery_set_scene(SCENE_DESERT);
    obstacles_set_scene(SCENE_DESERT, SCENE_DESERT, 0);
    obstacles_set_shadow_opacity(255);
    render_set_scene_mix(SCENE_DESERT, SCENE_DESERT, 0);
    s_death_feedback = false;
    s_death_feedback_s = 0;
    for (int i=0;i<5;i++) s_fragments[i].active=false;
}

// 设置菜单使用固定暖亮配色，不受昼夜调色影响。
static void draw_settings(void)
{
    const int px = 60, py = 66, pw = 200, ph = 108;
    // 面板 + 边框
    render_fill_rect(px, py, pw, ph, SETTINGS_PANEL);
    render_fill_rect(px, py, pw, 2, SETTINGS_INK);
    render_fill_rect(px, py + ph - 2, pw, 2, SETTINGS_INK);
    render_fill_rect(px, py, 2, ph, SETTINGS_INK);
    render_fill_rect(px + pw - 2, py, 2, ph, SETTINGS_INK);

    hud_text("SETTINGS", px + 14, py + 10, SETTINGS_INK);

    char buf[24];
    const char *labels[3] = { "INVINCIBLE", "VOLUME", "LIGHT" };
    for (int i = 0; i < 3; i++) {
        int ry = py + 34 + i * 22;
        if (i == 0)
            snprintf(buf, sizeof(buf), "%s < %s >", labels[i],
                     s_invincible_mode ? "ON" : "OFF");
        else if (i == 1)
            snprintf(buf, sizeof(buf), "%s < %d%% >", labels[i], VOL_LEVELS[s_vol_idx]);
        else
            snprintf(buf, sizeof(buf), "%s < %d%% >", labels[i], BL_LEVELS[s_bl_idx]);
        if (i == s_menu_row) {
            // 选中行反色
            render_fill_rect(px + 8, ry - 3, pw - 16, 16, SETTINGS_INK);
            hud_text(buf, px + 14, ry, SETTINGS_PANEL);
        } else {
            hud_text(buf, px + 14, ry, SETTINGS_INK);
        }
    }
}

static void draw_heart_pickup_fx(void)
{
    if (!s_heart_pickup_fx.active) return;
    float progress = (s_game_time_s - s_heart_pickup_fx.started_at) /
                     (HEART_FX_MS / 1000.0f);
    if (progress < 0) progress = 0;
    if (progress >= 1.0f) return;
    int w = OBS_HEART_BASE_W + (int)((32 - OBS_HEART_BASE_W) * progress + 0.5f);
    int h = OBS_HEART_BASE_H + (int)((29 - OBS_HEART_BASE_H) * progress + 0.5f);
    static const uint8_t OPACITY[4] = { 255, 192, 128, 64 };
    int stage = (int)(progress * 4.0f);
    render_sprite_scaled(&spr_heart, s_heart_pickup_fx.x - w / 2,
                         s_heart_pickup_fx.y - h / 2, w, h, OPACITY[stage]);
}

static void draw_lives(void)
{
    for (int i = 0; i < s_lives; i++) {
        int x = 6 + i * 15;
        if (!s_hud_heart_pop_active || i != s_hud_heart_pop_index) {
            render_sprite_raw(&spr_heart, x, 6);
            continue;
        }
        float progress = (s_game_time_s - s_hud_heart_pop_started_at) /
                         (HEART_FX_MS / 1000.0f);
        if (progress < 0) progress = 0;
        if (progress > 1) progress = 1;
        int w = 22 - (int)(11.0f * progress + 0.5f);
        int h = 20 - (int)(10.0f * progress + 0.5f);
        int lift = (int)(16.0f * progress * (1.0f - progress) + 0.5f);
        int cx = x + spr_heart.w / 2;
        int cy = 6 + spr_heart.h / 2;
        render_sprite_scaled_raw(&spr_heart, cx - w / 2, cy - h / 2 - lift,
                                 w, h, 255);
    }
}

static void draw_frame(int refresh_start_y)
{
    uint16_t sky = scene_day_color(SCENE_LAYER_SKY);
    uint16_t ground = scene_day_color(SCENE_LAYER_GROUND);
    uint16_t ink = day_cycle_color(&s_day_cycle, INK_COLORS);
    float segment_score = s_score - s_cycle_segment_start;
    float segment_limit = s_want_night ? NIGHT_SCORE_SPAN : DAY_SCORE_SPAN;
    float celestial_progress = segment_limit > 0 ? segment_score / segment_limit : 0.0f;
    if (celestial_progress < 0) celestial_progress = 0;
    if (celestial_progress > 1) celestial_progress = 1;

    // 背景: FAR_TOP 以下全是地面色, 远场带/河面覆盖上去
    render_begin(sky, ground, FAR_TOP);
    scenery_draw_sky(scene_day_color(SCENE_LAYER_CLOUD),
                     RGB565(120, 145, 180),
                     RGB565(210, 220, 240),
                     day_cycle_night_progress(&s_day_cycle), s_want_night,
                     celestial_progress, s_game_time_s);

    draw_background_layers(sky, scene_day_far_color(),
                           scene_day_color(SCENE_LAYER_HORIZON), ground);
    scenery_draw_far();
    draw_scene_landmarks(scene_manager_current(&s_scene), 255);
    if (scene_manager_transitioning(&s_scene))
        draw_scene_landmarks(scene_manager_next(&s_scene),
                             (uint8_t)(scene_manager_mix(&s_scene) * 255.0f + 0.5f));

    scenery_draw_ground_back(scene_day_color(SCENE_LAYER_SPECKLE));
    dust_draw(scene_day_color(SCENE_LAYER_DUST_NEAR), scene_day_color(SCENE_LAYER_DUST_FAR));

    uint8_t shadow = shadow_opacity();
    obstacles_set_shadow_opacity(shadow);
    obstacles_draw();
    fragments_draw();

    int dx, dy;
    player_draw_pos(&s_player, &dx, &dy);
    // 恐龙脚下的椭圆阴影(离地越高阴影越小越淡)
    {
        const sprite_t *sp = player_sprite(&s_player);
        int air = GROUND_Y - (int)s_player.y;
        int sh_w = sp->w * 4 / 5 - air / 3;
        if (sh_w < 8) sh_w = 8;
        render_set_opacity(shadow);
        render_fill_rect((int)s_player.x - sh_w / 2 + 4, GROUND_Y + 1,
                         sh_w, 3, scene_day_color(SCENE_LAYER_SPECKLE));
        render_set_opacity(255);
    }
    // 无敌期间恐龙闪烁(隔 100ms 隐去)
    bool blink_out = s_game_time_s < s_invincible_until &&
                     (((uint32_t)(s_game_time_s * 10.0f)) & 1);
    if (!blink_out)
        render_sprite(player_sprite(&s_player), dx, dy);

    scenery_draw_ground_front();

    draw_heart_pickup_fx();

    // 左上角红心(剩余生命)
    draw_lives();

    // HUD: 破整百时闪烁当前分
    bool blink = s_game_time_s < s_flash_until &&
                 (((uint32_t)(s_game_time_s * 5.0f)) & 1);
    hud_draw_scores((uint32_t)s_score, s_hi_score, !blink, ink);
    if (s_invincible_mode)
        hud_text("INV", 166, 8, RGB565(150, 240, 255));

    if (s_state == ST_PAUSED) hud_draw_paused(ink);
    if (s_state == ST_GAME_OVER) hud_draw_game_over(ink);
    if (s_state == ST_SETTINGS && s_settings_mix > 0) {
        uint8_t retain = (uint8_t)(255 - (127 * s_settings_mix + 127) / 255);
        render_dim(retain);
        render_set_opacity(s_settings_mix);
        draw_settings();
        render_set_opacity(255);
    }

    render_flush_from(refresh_start_y);
}

void game_run(void)
{
    render_init();
    input_init();
    sfx_init();
    fap_screenshot_init(); // 串口截屏协议(社区发布用)
    settings_load();
    settings_apply(); // 音量 + 背光按上次设置生效
    srand(esp_random());

    s_hi_score = hi_score_load();
    player_init(&s_player, DINO_X, GROUND_Y);
    obstacles_init(GROUND_Y);
    scenery_init(GROUND_Y, FAR_TOP, RIVER_Y, FIELD_Y);
    game_reset();
    s_state = ST_READY;

    int64_t last = esp_timer_get_time();
    int64_t next_frame_deadline_us = last;
    int64_t next_sky_refresh_us = last + SKY_REFRESH_US;
    int previous_dino_top;
    int previous_scenery_top = scenery_dynamic_top();
    float sky_scroll_px = 0;
    float sky_elapsed_s = 0;
    {
        int ignored_x;
        player_draw_pos(&s_player, &ignored_x, &previous_dino_top);
    }
    bool redraw_requested = true;
    idle_sleep_init(&s_idle_sleep, last, true);
    perf_reset();

    while (1) {
        if (s_state == ST_RUNNING) {
            int64_t now = esp_timer_get_time();
            int64_t wait_us = next_frame_deadline_us - now;
            if (wait_us > 0) {
                // 向上取整到系统的 1ms tick；绝对截止时间会抵消单次超调。
                TickType_t wait_ticks = pdMS_TO_TICKS((wait_us + 999) / 1000);
                if (wait_ticks > 0) vTaskDelay(wait_ticks);
            }
        }

        int64_t frame_start = esp_timer_get_time();
        float dt = (frame_start - last) / 1000000.0f;
        last = frame_start;
        if (dt > 0.1f) dt = 0.1f; // 防卡顿大步长穿墙

        game_key_t press = input_take_press();
        game_key_t click = input_take_click();
        game_key_t long_press = input_take_long();
        game_key_t held = input_held_key();
        bool was_consuming = idle_sleep_consumes_input(&s_idle_sleep);
        idle_sleep_event_t idle_event = idle_sleep_update(
            &s_idle_sleep, frame_start, sleep_eligible(s_state),
            press != KEY_NONE, held != KEY_NONE);

        if (idle_event == IDLE_SLEEP_EVENT_SLEEP) {
            bsp_display_backlight(0);
            ESP_LOGI(TAG, "非运行状态空闲30秒，关闭背光和画面刷新");
        } else if (idle_event == IDLE_SLEEP_EVENT_WAKE) {
            bsp_display_backlight((uint8_t)BL_LEVELS[s_bl_idx]);
            ESP_LOGI(TAG, "按键唤醒，恢复背光 %d%%", BL_LEVELS[s_bl_idx]);
        }

        if (was_consuming || idle_event != IDLE_SLEEP_EVENT_NONE ||
            idle_sleep_consumes_input(&s_idle_sleep)) {
            input_discard_events();
            if (idle_event == IDLE_SLEEP_EVENT_WAKE) {
                draw_frame(0);
                int ignored_x;
                player_draw_pos(&s_player, &ignored_x, &previous_dino_top);
                previous_scenery_top = scenery_dynamic_top();
                redraw_requested = false;
                fap_screenshot_pump();
            } else if (idle_sleep_is_asleep(&s_idle_sleep)) {
                // 截屏仍可导出息屏前冻结的最后一帧，不会重新推送 LCD。
                fap_screenshot_pump();
            }
            perf_reset();
            vTaskDelay(pdMS_TO_TICKS(STATIC_POLL_MS));
            continue;
        }

        bool up_held = held == KEY_UP;
        bool down_held = held == KEY_DOWN;
        game_state_t state_before = s_state;
        int lives_before = s_lives;
        bool transition_was_active = s_settings_fade.active;
        bool transition_redraw = settings_fade_update(frame_start);
        bool heart_effect_redraw = false;

        // 长按 OK: 进入/退出设置菜单(任何状态下可用)
        if (transition_was_active || s_settings_fade.active) {
            // 150ms 过渡期间忽略菜单/游戏输入；Power 键不经过本输入模块。
        } else if (long_press == KEY_OK) {
            if (s_state == ST_SETTINGS) {
                settings_save();
                settings_fade_start(false, frame_start);
            } else {
                // 运行中进入设置等价于先暂停; 退出后回到暂停
                s_return_state = (s_state == ST_RUNNING) ? ST_PAUSED : s_state;
                s_menu_row = 0;
                s_editing = false;
                s_state = ST_SETTINGS;
                settings_fade_start(true, frame_start);
            }
        } else switch (s_state) {
        case ST_READY:
            if (press == KEY_UP) {
                s_state = ST_RUNNING;
                player_queue_jump(&s_player);
                if (player_update(&s_player, dt, false, false, 0) &
                    PLAYER_EVENT_JUMPED) {
                    sfx_play(SFX_JUMP);
                    dust_emit_start((int)s_player.x, GROUND_Y);
                }
                dust_update(dt);
            }
            break;

        case ST_RUNNING:
            if (press == KEY_OK) { s_state = ST_PAUSED; break; }
            if (press == KEY_UP) player_queue_jump(&s_player);

            s_game_time_s += dt;
            fragments_update(dt);
            if (s_death_feedback) {
                s_death_feedback_s -= dt;
                if (s_death_feedback_s <= 0) {
                    s_death_feedback = false;
                    s_state = ST_GAME_OVER;
                }
                break;
            }
            heart_effect_redraw = heart_effects_update();

            // 分数与速度
            s_score += SCORE_RATE * dt;
            s_speed = SPEED_BASE + ((uint32_t)s_score / SCORE_PER_STEP) * SPEED_STEP;
            if (s_speed > SPEED_MAX) s_speed = SPEED_MAX;

            // 整百闪烁提示: 跨过整百后闪 ~1 秒
            {
                uint32_t step = (uint32_t)s_score / SCORE_PER_STEP;
                if (step > s_last_flash) {
                    s_last_flash = step;
                    s_flash_until = s_game_time_s + 1.0f;
                }
            }

            // 昼夜调色随天空的 100ms 节拍推进，避免局刷边界出现色带。
            float segment_score = s_score - s_cycle_segment_start;
            float segment_limit = s_want_night ? NIGHT_SCORE_SPAN : DAY_SCORE_SPAN;
            if (segment_score >= segment_limit) {
                bool was_night = s_want_night;
                s_cycle_segment_start = s_score;
                s_want_night = !s_want_night;
                if (was_night && !s_want_night)
                    s_scene_switch_pending = true;
            }
            sky_elapsed_s += dt;
            if (scene_manager_transitioning(&s_scene)) {
                scene_manager_update(&s_scene, dt, false, false);
                render_set_scene_mix(scene_manager_current(&s_scene),
                                     scene_manager_next(&s_scene),
                                     (uint8_t)(scene_manager_mix(&s_scene) * 255.0f + 0.5f));
                obstacles_set_scene(scene_manager_current(&s_scene),
                                    scene_manager_next(&s_scene),
                                    (uint8_t)(scene_manager_mix(&s_scene) * 255.0f + 0.5f));
                scenery_set_scene(scene_manager_current(&s_scene));
            }

            {
                player_event_t events = player_update(&s_player, dt, up_held,
                                                      down_held, speed_level());
                if (events & PLAYER_EVENT_LANDED)
                    dust_emit_land((int)s_player.x, GROUND_Y);
                if (events & PLAYER_EVENT_JUMPED)
                    sfx_play(SFX_JUMP);
            }
            obstacles_update(dt, s_speed, (uint32_t)s_score, speed_level(), s_lives);
            scenery_update(dt, s_speed);
            sky_scroll_px += s_speed * dt;
            dust_update(dt);

            // 碰撞: 红心=吃掉, 障碍=扣心(心碎裂消失 + 无敌 1.5s)
            {
                int hx, hy, hw, hh;
                player_hitbox(&s_player, &hx, &hy, &hw, &hh);
                int hit = obstacles_collide(hx, hy, hw, hh);
                if (hit >= 0) {
                    if (obstacles_type(hit) == OBS_HEART) {
                        int heart_x, heart_y;
                        obstacles_visual_center(hit, &heart_x, &heart_y);
                        obstacles_remove(hit);
                        if (s_lives < MAX_LIVES) {
                            s_hud_heart_pop_index = s_lives;
                            s_lives++;
                            s_heart_pickup_fx = (heart_pickup_fx_t) {
                                .active = true,
                                .started_at = s_game_time_s,
                                .x = heart_x,
                                .y = heart_y,
                            };
                            s_hud_heart_pop_active = true;
                            s_hud_heart_pop_started_at = s_game_time_s;
                            sfx_play(SFX_HEART);
                        }
                    } else if (!s_invincible_mode && s_game_time_s >= s_invincible_until) {
                        int fx, fy;
                        obstacles_visual_center(hit, &fx, &fy);
                        fragments_emit(fx, fy);
                        obstacles_remove(hit);
                        s_lives--;
                        if (s_lives <= 0) {
                            s_player.dead = true;
                            s_death_feedback = true;
                            s_death_feedback_s = 0.35f;
                            sfx_play(SFX_DEATH);
                            if ((uint32_t)s_score > s_hi_score) {
                                s_hi_score = (uint32_t)s_score;
                                hi_score_save(s_hi_score);
                            }
                        } else {
                            s_invincible_until = s_game_time_s + INVINCIBLE_MS / 1000.0f;
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
                if (press == KEY_UP) {
                    s_menu_row = (s_menu_row + 2) % 3;
                } else if (press == KEY_DOWN) {
                    s_menu_row = (s_menu_row + 1) % 3;
                }
                if (click == KEY_OK)
                    s_editing = true;
            } else {
                int dir = (press == KEY_UP) ? 1 : (press == KEY_DOWN) ? -1 : 0;
                if (dir != 0) {
                    if (s_menu_row == 0) {
                        s_invincible_mode = dir > 0;
                    } else if (s_menu_row == 1) {
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
                if (click == KEY_OK)
                    s_editing = false;
            }
            break;
        }

        if (s_state != state_before && sleep_eligible(s_state))
            idle_sleep_reset(&s_idle_sleep, frame_start);

        bool state_changed = s_state != state_before;
        bool input_activity = press != KEY_NONE || click != KEY_NONE ||
                              long_press != KEY_NONE;
        bool force_full_refresh = state_changed || s_lives != lives_before ||
                                  transition_redraw || s_hud_heart_pop_active ||
                                  heart_effect_redraw;

        if (s_state != ST_RUNNING && (force_full_refresh || input_activity))
            redraw_requested = true;

        int refresh_start_y = 0;
        int current_dino_top = previous_dino_top;
        int current_scenery_top = scenery_dynamic_top();
        bool should_draw = s_state == ST_RUNNING || redraw_requested;
        if (should_draw) {
            int ignored_x;
            player_draw_pos(&s_player, &ignored_x, &current_dino_top);

            if (s_state == ST_RUNNING) {
                if (state_before != ST_RUNNING) {
                    force_full_refresh = true;
                    next_sky_refresh_us = frame_start + SKY_REFRESH_US;
                    if (state_before == ST_READY || state_before == ST_GAME_OVER) {
                        sky_scroll_px = 0;
                        sky_elapsed_s = 0;
                    }
                } else if (frame_start >= next_sky_refresh_us) {
                    force_full_refresh = true;
                    scenery_update_sky(sky_scroll_px);
                    sky_scroll_px = 0;
                    day_cycle_update(&s_day_cycle, s_want_night, sky_elapsed_s);
                    scene_manager_update(&s_scene, 0,
                                         s_scene_switch_pending, false);
                    s_scene_switch_pending = false;
                    sky_elapsed_s = 0;
                    render_set_night_mix(day_cycle_night_mix(&s_day_cycle));
                    render_set_scene_mix(scene_manager_current(&s_scene),
                                         scene_manager_next(&s_scene),
                                         (uint8_t)(scene_manager_mix(&s_scene) * 255.0f + 0.5f));
                    obstacles_set_scene(scene_manager_current(&s_scene),
                                        scene_manager_next(&s_scene),
                                        (uint8_t)(scene_manager_mix(&s_scene) * 255.0f + 0.5f));
                    scenery_set_scene(scene_manager_current(&s_scene));
                    current_scenery_top = scenery_dynamic_top();
                    int64_t missed = (frame_start - next_sky_refresh_us) /
                                     SKY_REFRESH_US + 1;
                    next_sky_refresh_us += missed * SKY_REFRESH_US;
                }

                if (!force_full_refresh) {
                    refresh_start_y = FAR_TOP;
                    if (current_dino_top < refresh_start_y)
                        refresh_start_y = current_dino_top;
                    if (previous_dino_top < refresh_start_y)
                        refresh_start_y = previous_dino_top;
                    if (current_scenery_top < refresh_start_y)
                        refresh_start_y = current_scenery_top;
                    if (previous_scenery_top < refresh_start_y)
                        refresh_start_y = previous_scenery_top;
                    if (refresh_start_y < 0) refresh_start_y = 0;
                }
            }

            int64_t render_start = esp_timer_get_time();
            draw_frame(refresh_start_y);
            previous_dino_top = current_dino_top;
            previous_scenery_top = current_scenery_top;
            redraw_requested = false;
            fap_screenshot_pump(); // 若收到截屏请求, 此刻导出本帧
            int64_t frame_end = esp_timer_get_time();

            if (s_state == ST_RUNNING) {
                perf_record(frame_start, render_start, frame_end, refresh_start_y);
                if (state_before != ST_RUNNING)
                    next_frame_deadline_us = frame_start + RUN_FRAME_PERIOD_US;
                else
                    next_frame_deadline_us += RUN_FRAME_PERIOD_US;
                // 慢帧只丢弃已错过的截止点，不连续补跑多帧。
                if (next_frame_deadline_us < frame_end)
                    next_frame_deadline_us = frame_end;
            } else {
                perf_reset();
            }
        } else {
            fap_screenshot_pump();
            perf_reset();
        }

        if (s_state != ST_RUNNING)
            vTaskDelay(pdMS_TO_TICKS(STATIC_POLL_MS));
    }
}
