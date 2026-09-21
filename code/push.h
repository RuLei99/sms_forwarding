#ifndef PUSH_H
#define PUSH_H

#include "globals.h"

// 返回值：邮件是否发送成功（多轮重试后）
bool sendEmailNotification(const char* subject, const char* body);
// 返回值：位图 bit i = 通道 i+1 推送成功（MAX_PUSH_CHANNELS 位内有效）
uint8_t sendSMSToServer(const char* sender, const char* message, const char* timestamp);
// 返回值：该通道是否推送成功
bool sendToChannel(const PushChannel& channel, const char* sender, const char* message, const char* timestamp);
String urlEncode(const String& str);
String jsonEscape(const String& str);
String dingtalkSign(const String& secret, int64_t timestamp);
int64_t getUtcMillis();

#endif
