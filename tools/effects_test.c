// 宿主机场景效果测试：风滚草位移/滚动、星星固定布局和扬尘寿命。
// 用法: cc -std=c11 -I main tools/effects_test.c main/sprites.c -o /tmp/effects_test
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../main/render.h"

static int s_rect_count;
static int s_sprite_count;
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
    scenery_init(205, 86, 150, 174);
    for (int i = 0; i < DECOR_COUNT; i++) {
        s_decor[i].x = 100 + i * 40;
        s_decor[i].tumbleweed = false;
        s_decor[i].spr = &spr_rock_0;
        s_decor[i].anchor_y = 207;
    }
    decor_t *weed = &s_decor[0];
    weed->tumbleweed = true;
    weed->spr = &spr_tumbleweed_0;
    weed->roll_distance = 0;

    scenery_update(0.2f, 100.0f);
    assert(weed->x > 76.9f && weed->x < 77.1f); // 100 - 100*0.2*1.15
    assert(weed->spr == &spr_tumbleweed_1);

    s_sprite_count = 0;
    scenery_draw_ground_back(0);
    assert(s_sprite_count == GROUND_FAR_COUNT + DECOR_COUNT);
    assert(s_tumbleweed_y == 181); // anchor 207 - 51/2 - frame 1 bounce 1

    s_sprite_count = 0;
    scenery_draw_ground_front();
    assert(s_sprite_count == GROUND_NEAR_COUNT);
    assert(s_last_sprite_y >= 224); // K=3 近景严格限制在屏幕底部
}

static void test_depth_speeds(void)
{
    srand(2);
    scenery_init(205, 86, 150, 174);
    s_ground_far[0].x = 100.0f;
    s_decor[0].x = 100.0f;
    s_decor[0].tumbleweed = false;
    s_ground_near[0].x = 100.0f;

    scenery_update(0.1f, 100.0f);
    assert(s_ground_far[0].x > 95.49f && s_ground_far[0].x < 95.51f);
    assert(s_decor[0].x > 91.49f && s_decor[0].x < 91.51f);
    assert(s_ground_near[0].x > 87.99f && s_ground_near[0].x < 88.01f);

    float cloud_x = s_clouds[0].x;
    scenery_update(0.1f, 100.0f);
    assert(s_clouds[0].x == cloud_x); // 云只随天空低频节拍移动
    scenery_update_sky(10.0f);
    assert(s_clouds[0].x > cloud_x - 2.51f && s_clouds[0].x < cloud_x - 2.49f);
}

static void test_dynamic_top(void)
{
    srand(3);
    scenery_init(205, 86, 150, 174);
    for (int i = 0; i < FAR_COUNT; i++) s_far[i].x = -200;
    assert(scenery_dynamic_top() == RENDER_SCREEN_H);

    s_far[0].x = 100;
    s_far[0].y = 145;
    s_far[0].spr = &spr_tree_green_far;
    assert(scenery_dynamic_top() == 34);
}

static void test_stars(void)
{
    s_rect_count = 0;
    scenery_draw_sky(0, 1, 2, 0.49f, 3.0f);
    int day_rects = s_rect_count;
    assert(day_rects == CLOUD_COUNT * 2);

    s_rect_count = 0;
    s_rect_signature = 0;
    scenery_draw_sky(0, 1, 2, 1.0f, 3.0f);
    int night_rects = s_rect_count;
    uint32_t night_signature = s_rect_signature;
    assert(night_rects >= CLOUD_COUNT * 2 + STAR_COUNT);

    // 同一游戏时间重复绘制得到同一闪烁状态，暂停不会漂移。
    s_rect_count = 0;
    s_rect_signature = 0;
    scenery_draw_sky(0, 1, 2, 1.0f, 3.0f);
    assert(s_rect_count == night_rects);
    assert(s_rect_signature == night_signature);
}

static void test_dust(void)
{
    dust_reset();
    dust_emit_start(40, 205);
    s_rect_count = 0;
    dust_draw(1, 2);
    assert(s_rect_count == 7);
    dust_update(0.3f);
    s_rect_count = 0;
    dust_draw(1, 2);
    assert(s_rect_count == 0);

    dust_emit_land(40, 205);
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
