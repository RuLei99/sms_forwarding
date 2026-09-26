#ifndef HEALTH_H
#define HEALTH_H

#include "globals.h"

// 挂 WiFi 事件回调（只置标志，日志在主循环落盘）
void healthInit();
// 主循环中调用：模组巡检 / 后台重初始化 / WiFi 事件日志 / 堆内存水位
void healthTask();

// 最近一次巡检到的信号（如 "-77 dBm"），供 /status 展示
extern String modemSignalCache;

#endif
