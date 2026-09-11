// main/sfx.h
// 音效:跳跃、拾取红心与死亡。
// PCM 在初始化时合成到静态缓冲,播放走独立任务,不阻塞游戏循环。
#pragma once

#include <stdint.h>

typedef enum {
    SFX_JUMP = 0,
    SFX_HEART,
    SFX_DEATH,
} sfx_id_t;

// 初始化音频 codec 与播放任务。硬件无声卡时可安全失败(游戏静默运行)。
void sfx_init(void);

// 投递一个音效(非阻塞; 正在播放时新音效会丢弃)。
void sfx_play(sfx_id_t id);

// 设置输出音量 0..100(%), 立即生效。
void sfx_set_volume(uint8_t percent);
