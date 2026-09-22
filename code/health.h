#ifndef HEALTH_H
#define HEALTH_H

#include "globals.h"

// 挂 WiFi 事件回调（只置标志，日志在主循环落盘）
void healthInit();
// 主循环中调用：模组巡检 / 后台重初始化 / WiFi 事件日志 / 堆内存水位
void healthTask();

// 最近一次巡检到的信号（如 "-77 dBm"），供 /status 展示
extern String modemSignalCache;

// 每日统计（报告发送后清零）
struct HealthDayStats {
  uint16_t smsIn;      // 收到短信（进入处理流程）
  uint16_t fwdOk;      // 转发成功（邮件或任一通道成功）
  uint16_t fwdFail;    // 全部通道+邮件均失败
  uint16_t blocked;    // 被黑名单/关键词拦截
  int sigMin, sigMax;  // 信号范围（巡检采样，0=无样本）
  uint32_t heapMin;    // 堆内存最低值
};
extern HealthDayStats dayStats;

// 立即发送健康报告（每日定时或 REPORT 命令触发），发送后清零统计
void healthSendReport();

#endif
