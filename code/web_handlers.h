#ifndef WEB_HANDLERS_H
#define WEB_HANDLERS_H

#include "globals.h"

#define LOG_BUF_SIZE 120

extern String logBuffer[LOG_BUF_SIZE];
extern int logBufIdx;
extern int logBufCount;

void logCapture(const String& msg);
void logCapture(const char* msg);
void logCaptureF(const char* fmt, ...);
void logCaptureLn(const String& msg);
void logCaptureLn(const char* msg);

bool checkAuth();
void handleRoot();
void handleToolsPage();
void handleSave();
void handleQuery();
void handleFlightMode();
void handleATCommand();
void handleSendSms();
void handlePing();
void handleLog();
void handleModem();
void handleWifi();
void handleSystem();
void handleDataLock();
void handleStatus();     // 概览页轻量状态 JSON（自动刷新用）
void handleSmsLog();     // 短信记录查询/清空
void handleTestPush();   // 推送通道测试

#endif
