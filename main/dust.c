#include "dust.h"
#include "render.h"

#include <stdbool.h>

#define DUST_CAPACITY 12
#define DUST_GRAVITY  90.0f

typedef struct {
    float x, y;
    float vx, vy;
    float age, life;
    uint8_t size;
    bool active;
} dust_particle_t;

static dust_particle_t s_particles[DUST_CAPACITY];
static uint8_t s_burst_serial;

static const int8_t X_OFF[] = { -6, -2, 2, 5, -4, 1, 7 };
static const int8_t VX_START[] = { -96, -78, -62, -45, -86, -54, 12 };
static const int8_t VX_LAND[] = { -66, -48, -30, 18, -56, -38, 24 };
static const int8_t VY_START[] = { -38, -54, -30, -46, -24, -58, -34 };
static const int8_t VY_LAND[] = { -25, -38, -20, -32, -27, -35, -22 };
static const uint16_t LIFE_MS[] = { 180, 220, 250, 200, 260, 235, 190 };

void dust_reset(void)
{
    for (int i = 0; i < DUST_CAPACITY; i++) s_particles[i].active = false;
    s_burst_serial = 0;
}

static dust_particle_t *take_particle(void)
{
    for (int i = 0; i < DUST_CAPACITY; i++)
        if (!s_particles[i].active) return &s_particles[i];

    // 理论上两次落地相隔远大于粒子寿命；若极端卡顿重叠，覆盖最老的一粒。
    dust_particle_t *oldest = &s_particles[0];
    for (int i = 1; i < DUST_CAPACITY; i++)
        if (s_particles[i].age > oldest->age) oldest = &s_particles[i];
    return oldest;
}

static void emit(int foot_x, int foot_y, bool strong)
{
    int count = strong ? 7 : 5;
    for (int i = 0; i < count; i++) {
        int pattern = (i + s_burst_serial) % 7;
        dust_particle_t *p = take_particle();
        p->x = (float)(foot_x + X_OFF[pattern]);
        p->y = (float)(foot_y - 2 - (pattern & 1));
        p->vx = (float)(strong ? VX_START[pattern] : VX_LAND[pattern]);
        p->vy = (float)(strong ? VY_START[pattern] : VY_LAND[pattern]);
        p->age = 0;
        p->life = LIFE_MS[pattern] / 1000.0f;
        p->size = (uint8_t)(strong && (pattern % 3 == 0) ? 3 : 2);
        p->active = true;
    }
    s_burst_serial = (uint8_t)((s_burst_serial + 2) % 7);
}

void dust_emit_start(int foot_x, int foot_y)
{
    emit(foot_x, foot_y, true);
}

void dust_emit_land(int foot_x, int foot_y)
{
    emit(foot_x, foot_y, false);
}

void dust_update(float dt)
{
    if (dt <= 0) return;
    for (int i = 0; i < DUST_CAPACITY; i++) {
        dust_particle_t *p = &s_particles[i];
        if (!p->active) continue;
        p->age += dt;
        if (p->age >= p->life) {
            p->active = false;
            continue;
        }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vy += DUST_GRAVITY * dt;
        p->vx *= 1.0f - 1.8f * dt;
    }
}

void dust_draw(uint16_t near_color, uint16_t far_color)
{
    for (int i = 0; i < DUST_CAPACITY; i++) {
        dust_particle_t *p = &s_particles[i];
        if (!p->active) continue;
        bool fading = p->age > p->life * 0.55f;
        int size = fading ? 1 : p->size;
        render_fill_rect((int)p->x, (int)p->y, size, size,
                         fading ? far_color : near_color);
    }
}
