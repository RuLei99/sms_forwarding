#ifndef SMS_PROCESS_H
#define SMS_PROCESS_H

#include "globals.h"

void initConcatBuffer();
int findOrCreateConcatSlot(int refNumber, const char* sender, int totalParts);
String assembleConcatSms(int slot);
void clearConcatSlot(int slot);
void checkConcatTimeout();
// readSerialLine 内部使用跨调用静态缓冲（非重入），不对外导出，统一走 checkSerial1URC()
bool isHexString(const String& str);
bool isInNumberBlackList(const char* sender);
bool isAdmin(const char* sender);
void processAdminCommand(const char* sender, const char* text);
void processSmsContent(const char* sender, const char* text, const char* timestamp);
void checkSerial1URC();
// 从 AT 响应文本中提取 +CMTI，及 +CMT 后紧随的 PDU 入队
// （AT 忙期间到达的短信不丢失）
void smsNoteCmtiFromResponse(const String& resp);

#endif
