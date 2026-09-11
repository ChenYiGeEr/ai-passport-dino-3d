// 宿主机场景效果测试：风滚草位移/滚动、星星固定布局和扬尘寿命。
// 用法: cc -std=c11 -I main tools/effects_test.c main/sprites.c -o /tmp/effects_test
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../main/render.h"

static int s_rect_count;
static int s_sprite_count;
static int s_raw_sprite_count;
static int s_first_sprite_y;
static int s_last_sprite_y;
static int s_tumbleweed_y;
static uint32_t s_rect_signature;
void render_sprite(const sprite_t *spr, int x, int y)
{
    (void)x;
    if (s_sprite_count == 0) s_first_sprite_y = y;
    s_last_sprite_y = y;
    if (spr == &spr_tumbleweed_1) s_tumbleweed_y = y;
    s_sprite_count++;
}
void render_sprite_scaled(const sprite_t *spr, int x, int y, int w, int h, uint8_t opacity)
{
    (void)spr; (void)x; (void)y; (void)w; (void)h; (void)opacity;
    s_sprite_count++;
}
void render_sprite_raw(const sprite_t *spr, int x, int y)
{
    (void)spr; (void)x; (void)y;
    s_raw_sprite_count++;
}
void render_set_opacity(uint8_t opacity) { (void)opacity; }
void render_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    s_rect_count++;
    s_rect_signature = s_rect_signature * 33u + (uint32_t)(x * 3 + y * 5 + w * 7 + h * 11 + color);
}

// 直接包含实现，以验证固定容量状态；固件仍按正常独立编译单元构建。
#include "../main/scenery.c"
#include "../main/dust.c"

static void test_tumbleweed(void)
{
    srand(1);
    scenery_init(216, 88, 160, 160);
    tumbleweed_t *weed = &s_tumble_mid[0];
    weed->x = 100;
    weed->spr = &spr_tumbleweed_0;
    weed->roll_distance = 0;

    scenery_update(0.2f, 100.0f);
    assert(weed->x > 82.7f && weed->x < 82.9f); // 100 - 100*0.2*0.75*1.15
    assert(weed->spr == &spr_tumbleweed_1);

    s_sprite_count = 0;
    scenery_draw_ground_back(0);
    assert(s_sprite_count == DECOR_COUNT + TUMBLE_MID_COUNT);

    s_sprite_count = 0;
    scenery_draw_ground_front();
    assert(s_sprite_count == GROUND_NEAR_COUNT + TUMBLE_NEAR_COUNT);
    assert(s_tumbleweed_y == 185); // y=216，帧1 下移 3px
    assert(s_last_sprite_y >= 170); // 风滚草底部贴近地面线 y=216
}

static void test_depth_speeds(void)
{
    srand(2);
    scenery_init(216, 88, 160, 160);
    s_ground_far[0].x = 100.0f;
    s_decor[0].x = 100.0f;
    s_ground_near[0].x = 100.0f;

    scenery_update(0.1f, 100.0f);
    assert(s_ground_far[0].x == 100.0f);
    assert(s_decor[0].x > 92.49f && s_decor[0].x < 92.51f);
    assert(s_ground_near[0].x > 88.99f && s_ground_near[0].x < 89.01f);

    float cloud_x = s_clouds[0].x;
    scenery_update(0.1f, 100.0f);
    assert(s_clouds[0].x == cloud_x); // 云只随天空低频节拍移动
    scenery_update_sky(10.0f);
    assert(s_clouds[0].x > cloud_x - 2.51f && s_clouds[0].x < cloud_x - 2.49f);
}

static void test_dynamic_top(void)
{
    srand(3);
    scenery_init(216, 88, 160, 160);
    for (int i = 0; i < FAR_COUNT; i++) s_far[i].x = -200;
    assert(scenery_dynamic_top() == RENDER_SCREEN_H);

    s_far[0].x = 100;
    s_far[0].y = 145;
    s_far[0].spr = &spr_dry_grass_far;
    assert(scenery_dynamic_top() == 131);
}

static void test_stars(void)
{
    s_rect_count = 0;
    scenery_draw_sky(0, 1, 2, 0.49f, false, 0.5f, 3.0f);
    int day_rects = s_rect_count;
    assert(day_rects == CLOUD_COUNT * 2); // 日间太阳改为离线精灵
    assert(s_raw_sprite_count == 1);

    s_rect_count = 0;
    s_rect_signature = 0;
    s_raw_sprite_count = 0;
    scenery_draw_sky(0, 1, 2, 1.0f, true, 0.5f, 3.0f);
    int night_rects = s_rect_count;
    uint32_t night_signature = s_rect_signature;
    assert(night_rects >= STAR_COUNT); // 夜间隐藏云，只保留星星
    assert(s_raw_sprite_count == 1);

    // 同一游戏时间重复绘制得到同一闪烁状态，暂停不会漂移。
    s_rect_count = 0;
    s_rect_signature = 0;
    s_raw_sprite_count = 0;
    scenery_draw_sky(0, 1, 2, 1.0f, true, 0.5f, 3.0f);
    assert(s_rect_count == night_rects);
    assert(s_rect_signature == night_signature);
}

static void test_dust(void)
{
    dust_reset();
    dust_emit_start(40, 216);
    s_rect_count = 0;
    dust_draw(1, 2);
    assert(s_rect_count == 7);
    dust_update(0.3f);
    s_rect_count = 0;
    dust_draw(1, 2);
    assert(s_rect_count == 0);

    dust_emit_land(40, 216);
    s_rect_count = 0;
    dust_draw(1, 2);
    assert(s_rect_count == 5);
}

int main(void)
{
    test_tumbleweed();
    test_depth_speeds();
    test_dynamic_top();
    test_stars();
    test_dust();
    puts("effects_test: ok");
    return 0;
}
