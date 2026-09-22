#ifndef RECORDS_H
#define RECORDS_H

#include "globals.h"

#define SMS_RECORD_MAX 50   // 内存中最多保留的短信条数（环形覆盖）
// LittleFS 持久化文件（records.cpp 读写，web_handlers 导出共用）
#define RECORDS_FILE "/records.log"
#define RECORDS_OLD "/records.1.log"

// 单条短信记录（纯内存，重启清空；flash 不做持久化，避免写磨损）
struct SmsRecord {
  bool valid;
  String sender;
  String text;
  String timestamp;
  bool emailOk;        // 邮件转发结果
  uint8_t pushMask;    // 位图 bit i = 通道 i+1 推送成功
  uint8_t pushEnabled; // 位图 bit i = 通道 i+1 当时已启用（决定结果如何展示）
};

// 记录一条已转发的短信（转发完成后调用，带上各通道结果）
void recordsAdd(const char* sender, const char* text, const char* timestamp,
                bool emailOk, uint8_t pushMask, uint8_t pushEnabled);
// 生成 JSON（最新在前）：{"total":n,"items":[{"s":...,"t":...,"ts":...,"e":0/1,"p":结果位图,"en":启用位图}]}
String recordsJson();
// 当前条数
int recordsCount();
// 清空（内存 + 持久化文件）
void recordsClear();
// 启动时从 LittleFS 加载历史到内存环形缓冲
void recordsLoad();

#endif
