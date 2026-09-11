#include "scene.h"
#include "render.h"
#include <stdlib.h>
#include <math.h>

/* Four compact palettes: day, sunset, dusk, night. */
static const uint16_t PALETTE[SCENE_COUNT][10][4] = {
    { {RGB565(170,210,232),RGB565(142,172,204),RGB565(54,68,92),RGB565(10,14,28)},
      {RGB565(214,178,90),RGB565(190,112,68),RGB565(76,54,82),RGB565(20,20,52)},
      {RGB565(127,196,214),RGB565(91,145,166),RGB565(48,71,112),RGB565(24,40,80)},
      {RGB565(232,192,100),RGB565(204,142,76),RGB565(91,61,76),RGB565(24,24,56)},
      {RGB565(200,165,85),RGB565(180,120,72),RGB565(104,78,105),RGB565(120,120,160)},
      {RGB565(180,150,90),RGB565(166,108,69),RGB565(77,55,74),RGB565(44,43,72)},
      {RGB565(211,170,86),RGB565(192,125,70),RGB565(102,70,80),RGB565(54,50,72)},
      {RGB565(236,202,116),RGB565(218,153,91),RGB565(130,91,105),RGB565(76,71,96)},
      {RGB565(255,250,235),RGB565(255,214,180),RGB565(151,122,145),RGB565(80,80,110)},
      {RGB565(60,50,30),RGB565(67,43,28),RGB565(170,154,170),RGB565(200,200,220)} },
    { {RGB565(170,210,232),RGB565(142,172,204),RGB565(54,68,92),RGB565(10,14,28)},
      {RGB565(157,111,91),RGB565(190,91,70),RGB565(69,56,81),RGB565(22,26,52)},
      {RGB565(116,153,177),RGB565(91,117,151),RGB565(48,65,101),RGB565(26,40,76)},
      {RGB565(183,116,76),RGB565(169,91,65),RGB565(73,57,76),RGB565(25,27,56)},
      {RGB565(139,111,111),RGB565(159,93,80),RGB565(85,75,106),RGB565(112,126,157)},
      {RGB565(114,123,151),RGB565(133,89,94),RGB565(72,65,92),RGB565(44,51,79)},
      {RGB565(157,112,84),RGB565(171,91,72),RGB565(96,73,94),RGB565(52,54,76)},
      {RGB565(190,160,126),RGB565(204,127,98),RGB565(122,107,132),RGB565(74,80,104)},
      {RGB565(238,245,248),RGB565(255,202,183),RGB565(160,151,177),RGB565(88,102,126)},
      {RGB565(44,48,58),RGB565(55,39,40),RGB565(157,159,178),RGB565(200,210,220)} },
    { {RGB565(170,210,232),RGB565(142,172,204),RGB565(54,68,92),RGB565(10,14,28)},
      {RGB565(188,157,112),RGB565(221,126,86),RGB565(67,91,99),RGB565(22,46,61)},
      {RGB565(97,171,176),RGB565(74,140,155),RGB565(44,87,108),RGB565(22,55,76)},
      {RGB565(126,194,132),RGB565(117,163,115),RGB565(56,99,90),RGB565(24,59,68)},
      {RGB565(104,160,132),RGB565(171,130,99),RGB565(76,114,111),RGB565(101,145,156)},
      {RGB565(87,146,120),RGB565(159,112,91),RGB565(61,100,96),RGB565(41,68,81)},
      {RGB565(121,179,123),RGB565(169,130,95),RGB565(72,111,96),RGB565(48,80,87)},
      {RGB565(172,214,159),RGB565(202,158,121),RGB565(111,150,133),RGB565(73,105,119)},
      {RGB565(247,255,238),RGB565(255,221,184),RGB565(171,190,173),RGB565(100,145,150)},
      {RGB565(31,61,45),RGB565(57,54,37),RGB565(156,181,165),RGB565(195,220,214)} },
    { {RGB565(170,210,232),RGB565(142,172,204),RGB565(54,68,92),RGB565(10,14,28)},
      {RGB565(153,81,65),RGB565(191,71,51),RGB565(62,42,52),RGB565(22,17,31)},
      {RGB565(82,90,95),RGB565(87,67,76),RGB565(46,42,60),RGB565(21,24,43)},
      {RGB565(73,70,65),RGB565(102,55,46),RGB565(48,38,49),RGB565(22,20,35)},
      {RGB565(83,76,71),RGB565(127,66,58),RGB565(66,55,71),RGB565(99,94,108)},
      {RGB565(76,70,64),RGB565(123,56,47),RGB565(57,45,58),RGB565(42,38,51)},
      {RGB565(90,78,63),RGB565(135,62,47),RGB565(64,46,53),RGB565(45,40,51)},
      {RGB565(125,117,91),RGB565(154,75,60),RGB565(91,77,89),RGB565(69,63,79)},
      {RGB565(232,236,214),RGB565(255,192,164),RGB565(172,148,158),RGB565(113,106,126)},
      {RGB565(37,36,31),RGB565(53,29,25),RGB565(151,140,151),RGB565(207,199,184)} },
};

void scene_manager_reset(scene_manager_t *m) {
    m->current = SCENE_DESERT; m->next = SCENE_DESERT;
    m->transition_s = 0; m->transitioning = false; m->night_latched = false;
    m->bag_mask = 1u << SCENE_DESERT;
}

void scene_manager_begin(scene_manager_t *m, scene_id_t next) {
    if (m->transitioning || next == m->current) return;
    m->next = next; m->transition_s = 0; m->transitioning = true;
}

void scene_manager_update(scene_manager_t *m, float dt, bool deepest_night, bool blocked) {
    if (!deepest_night) m->night_latched = false;
    if (m->transitioning) {
        if (!blocked && dt > 0) m->transition_s += dt;
        if (m->transition_s >= SCENE_TRANSITION_S) {
            m->current = m->next; m->transitioning = false;
            m->transition_s = 0; m->bag_mask |= 1u << m->current;
        }
        return;
    }
    if (!deepest_night || blocked || m->night_latched) return;
    m->night_latched = true;
    if (m->bag_mask == ((1u << SCENE_COUNT) - 1u)) m->bag_mask = 1u << m->current;
    scene_id_t pick = m->current;
    int choices[SCENE_COUNT], n = 0;
    for (int i = 0; i < SCENE_COUNT; i++) if (!(m->bag_mask & (1u << i)) && i != m->current) choices[n++] = i;
    if (n) pick = (scene_id_t)choices[rand() % n];
    scene_manager_begin(m, pick);
}

float scene_manager_mix(const scene_manager_t *m) { return m->transitioning ? m->transition_s / SCENE_TRANSITION_S : 0.0f; }
scene_id_t scene_manager_current(const scene_manager_t *m) { return m->current; }
scene_id_t scene_manager_next(const scene_manager_t *m) { return m->next; }
bool scene_manager_transitioning(const scene_manager_t *m) { return m->transitioning; }

uint16_t scene_color(scene_id_t scene, int layer, int stage) {
    if (scene < 0 || scene >= SCENE_COUNT || layer < 0 || layer >= 10) return 0;
    if (stage < 0) stage = 0;
    if (stage > 3) stage = 3;
    return PALETTE[scene][layer][stage];
}

void scene_sprite_tint(scene_id_t scene, uint8_t *r, uint8_t *g, uint8_t *b) {
    // 中景/主体精灵随场景换色：峡谷红褐、绿洲青绿、火山暗红灰，
    // 避免所有场景都被沙漠黄的默认色污染。
    static const uint8_t T[SCENE_COUNT][3] = {
        {255,255,255}, /* 沙漠：保留黄棕恐龙原色 */
        {255,220,205}, /* 峡谷：暖浅色，避免融入红褐背景 */
        {225,255,210}, /* 绿洲：提高亮度和黄绿色辨识度 */
        {255,215,195}, /* 火山：亮橙灰，避开暗红远景 */
    };
    *r = T[scene][0]; *g = T[scene][1]; *b = T[scene][2];
}
