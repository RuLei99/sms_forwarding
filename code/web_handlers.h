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
void handleFlightMode();
void handleATCommand();
void handleSendSms();
void handlePing();
void handleLog();
void handleModem();
void handleWifi();
void handleSystem();
void handleJobStatus();   // 任务队列状态轮询（/job?id=）
void handleStatus();     // 概览页轻量状态 JSON（自动刷新用）
void handleSmsLog();     // 短信记录查询/清空
void handleTestPush();   // 推送通道测试
void handleRecordsExport();  // 短信记录导出 CSV
void handleConfigExport();   // 配置导出 JSON
void handleConfigImport();   // 配置导入 JSON

#endif
