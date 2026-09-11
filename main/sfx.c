// main/sfx.c
#include "sfx.h"
#include "bsp_audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>

#define SFX_RATE     22050
#define SFX_BITS     16
#define SFX_CH       1
#define JUMP_MS      120
#define HEART_MS     160
#define DEATH_MS     400
#define AMP          12000
#define VOLUME       40

static const char *TAG = "sfx";
static int16_t s_jump_pcm[SFX_RATE * JUMP_MS / 1000];
static int16_t s_heart_pcm[SFX_RATE * HEART_MS / 1000];
static int16_t s_death_pcm[SFX_RATE * DEATH_MS / 1000];
static QueueHandle_t s_queue;
static bool s_ready;

// 简单方波合成, 频率从 f0 线性滑到 f1
static void synth_slide(int16_t *out, int n, float f0, float f1)
{
    float phase = 0;
    for (int i = 0; i < n; i++) {
        float f = f0 + (f1 - f0) * i / n;
        phase += f / SFX_RATE;
        if (phase >= 1.0f) phase -= 1.0f;
        // 方波 + 尾部衰减, 避免爆音
        float env = 1.0f - (float)i / n * 0.5f;
        out[i] = (int16_t)((phase < 0.5f ? AMP : -AMP) * env);
    }
}

// 两个连续上扬短音；每段独立衰减，和跳跃滑音保持明显区别。
static void synth_heart(int16_t *out, int n)
{
    float phase = 0;
    int half = n / 2;
    for (int i = 0; i < n; i++) {
        int local = i < half ? i : i - half;
        int length = i < half ? half : n - half;
        float f = i < half ? 760.0f : 1140.0f;
        if (i == half) phase = 0;
        phase += f / SFX_RATE;
        if (phase >= 1.0f) phase -= 1.0f;
        float env = 1.0f - (float)local / length * 0.7f;
        out[i] = (int16_t)((phase < 0.5f ? AMP : -AMP) * env);
    }
}

static void sfx_task(void *arg)
{
    int id;
    while (xQueueReceive(s_queue, &id, portMAX_DELAY)) {
        if (id == SFX_JUMP) {
            bsp_audio_write(s_jump_pcm, sizeof(s_jump_pcm));
        } else if (id == SFX_HEART) {
            bsp_audio_write(s_heart_pcm, sizeof(s_heart_pcm));
        } else {
            bsp_audio_write(s_death_pcm, sizeof(s_death_pcm));
        }
    }
}

void sfx_init(void)
{
    synth_slide(s_jump_pcm, sizeof(s_jump_pcm) / 2, 500, 1100);  // 上升短音
    synth_heart(s_heart_pcm, sizeof(s_heart_pcm) / 2);
    synth_slide(s_death_pcm, sizeof(s_death_pcm) / 2, 800, 150); // 下滑长音

    if (bsp_audio_init() != ESP_OK ||
        bsp_audio_set_format(SFX_RATE, SFX_BITS, SFX_CH) != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed, running silent");
        return;
    }
    bsp_audio_set_volume(VOLUME);
    s_queue = xQueueCreate(2, sizeof(int));
    xTaskCreate(sfx_task, "sfx", 4096, NULL, 5, NULL);
    s_ready = true;
}

void sfx_play(sfx_id_t id)
{
    if (!s_ready) return;
    int v = (int)id;
    xQueueSend(s_queue, &v, 0); // 满了就丢, 不阻塞游戏
}

void sfx_set_volume(uint8_t percent)
{
    if (!s_ready) return;
    bsp_audio_set_volume(percent);
}
