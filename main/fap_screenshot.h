// main/fap_screenshot.h
// FAP_SCREENSHOT_V1 串口截屏协议(FoloToy AI Passport 社区发布要求)。
// 主机发来 "FAP_SCREENSHOT_V1\n", 固件回一帧 RGB565LE 画面。
// 纯只读: 不重启、不改设置、不暴露任何凭据。
#pragma once

// 启动监听任务(USB-Serial-JTAG 控制台口, 115200)。
void fap_screenshot_init(void);

// 由游戏循环每帧调用: 有待处理的截屏请求时写出整帧。
void fap_screenshot_pump(void);
