#ifndef PUSH_H
#define PUSH_H

#include "globals.h"

// 返回值：邮件是否发送成功（多轮重试后）
bool sendEmailNotification(const char* subject, const char* body);
// 返回值：位图 bit i = 通道 i+1 推送成功（MAX_PUSH_CHANNELS 位内有效）
uint8_t sendSMSToServer(const char* sender, const char* message, const char* timestamp);
// 返回值：该通道是否推送成功（idx 为通道下标，用于健康统计；-1 不统计）
bool sendToChannel(const PushChannel& channel, const char* sender, const char* message, const char* timestamp, int idx = -1);
String urlEncode(const String& str);
String jsonEscape(const String& str);
String dingtalkSign(const String& secret, int64_t timestamp);
int64_t getUtcMillis();

// ---- 通道类型描述符表（插件化）：枚举-名称-字段需求-请求构造的单一事实来源 ----
size_t pushTypeCount();
const char* pushTypeName(PushType t);
// 按描述符表校验通道字段是否齐全（config.cpp 的 isPushChannelValid 委托到这里）
bool pushChannelFieldsValid(const PushChannel& ch);

// ---- 通道健康统计与熔断（运行时，不持久化） ----
bool pushChannelCooling(int idx);   // 该通道是否处于熔断冷却中
String pushChannelStatsJson();      // 概览页通道健康数据
void pushStatsNoteBlocked(int idx); // 短信被熔断跳过时计数

#endif
