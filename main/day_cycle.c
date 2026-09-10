#include "day_cycle.h"

void day_cycle_reset(day_cycle_t *cycle)
{
    cycle->phase = 0;
}

void day_cycle_update(day_cycle_t *cycle, bool want_night, float dt)
{
    if (dt <= 0) return;

    float delta = DAY_CYCLE_LAST_STAGE * dt / DAY_CYCLE_DURATION_S;
    if (want_night) {
        cycle->phase += delta;
        if (cycle->phase > DAY_CYCLE_LAST_STAGE)
            cycle->phase = DAY_CYCLE_LAST_STAGE;
    } else {
        cycle->phase -= delta;
        if (cycle->phase < 0)
            cycle->phase = 0;
    }
}

static uint16_t lerp_rgb565(uint16_t a, uint16_t b, uint8_t mix)
{
    int ar = (a >> 11) & 31;
    int ag = (a >> 5) & 63;
    int ab = a & 31;
    int br = (b >> 11) & 31;
    int bg = (b >> 5) & 63;
    int bb = b & 31;

    int r = ar + (br - ar) * mix / 255;
    int g = ag + (bg - ag) * mix / 255;
    int blue = ab + (bb - ab) * mix / 255;
    return (uint16_t)((r << 11) | (g << 5) | blue);
}

uint16_t day_cycle_color(const day_cycle_t *cycle,
                         const uint16_t colors[DAY_CYCLE_STAGE_COUNT])
{
    int stage = (int)cycle->phase;
    if (stage <= 0 && cycle->phase <= 0) return colors[0];
    if (stage >= DAY_CYCLE_STAGE_COUNT - 1) return colors[DAY_CYCLE_STAGE_COUNT - 1];

    float local = cycle->phase - stage;
    uint8_t mix = (uint8_t)(local * 255.0f + 0.5f);
    return lerp_rgb565(colors[stage], colors[stage + 1], mix);
}

float day_cycle_night_progress(const day_cycle_t *cycle)
{
    return cycle->phase / DAY_CYCLE_LAST_STAGE;
}

uint8_t day_cycle_night_mix(const day_cycle_t *cycle)
{
    float progress = day_cycle_night_progress(cycle);
    return (uint8_t)(progress * 255.0f + 0.5f);
}
