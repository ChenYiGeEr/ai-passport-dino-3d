#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SCENE_DESERT = 0,
    SCENE_CANYON,
    SCENE_OASIS,
    SCENE_VOLCANO,
    SCENE_COUNT,
} scene_id_t;

typedef struct {
    scene_id_t current;
    scene_id_t next;
    float transition_s;
    bool transitioning;
    bool night_latched;
    uint8_t bag_mask;
} scene_manager_t;

void scene_manager_reset(scene_manager_t *m);
void scene_manager_update(scene_manager_t *m, float dt, bool deepest_night,
                          bool blocked);
void scene_manager_begin(scene_manager_t *m, scene_id_t next);
float scene_manager_mix(const scene_manager_t *m);
scene_id_t scene_manager_current(const scene_manager_t *m);
scene_id_t scene_manager_next(const scene_manager_t *m);
bool scene_manager_transitioning(const scene_manager_t *m);
uint16_t scene_color(scene_id_t scene, int layer, int stage);
void scene_sprite_tint(scene_id_t scene, uint8_t *r, uint8_t *g, uint8_t *b);

#define SCENE_TRANSITION_S 2.0f
#define SCENE_LAYER_SKY 0
#define SCENE_LAYER_FAR 1
#define SCENE_LAYER_RIVER 2
#define SCENE_LAYER_GROUND 3
#define SCENE_LAYER_HORIZON 4
#define SCENE_LAYER_SPECKLE 5
#define SCENE_LAYER_DUST_NEAR 6
#define SCENE_LAYER_DUST_FAR 7
#define SCENE_LAYER_CLOUD 8
#define SCENE_LAYER_INK 9
