#ifndef MODEM_H
#define MODEM_H

#include "globals.h"

String sendATCommand(const char* cmd, unsigned long timeout);
void modemPowerCycle();
void resetModule();
bool modemInit();
bool sendATandWaitOK(const char* cmd, unsigned long timeout);
bool waitCEREG();
void blink_short(unsigned long gap_time = 500);
bool sendSMS(const char* phoneNumber, const char* message);

// ---- 共享 AT 响应解析（初始化 / 健康巡检 / Web 查询共用） ----
// AT+COPS? 响应 → 运营商名，失败返回空串
String modemParseCops(const String& resp);
// AT+CSQ 响应 → dBm，成功返回 true（rssi=99/解析失败返回 false）
bool modemParseCsq(const String& resp, int& dbm);
// 发送 AT+GSN 并解析 IMEI（调用方须持有串口占用权），失败返回空串
String modemQueryImei();

// 模组信息缓存：初始化成功后填充（IMEI/ICCID/型号不变），运营商由健康巡检刷新
extern String modemImeiCache;      // AT+GSN
extern String modemOperatorCache;  // AT+COPS?
extern String modemIccidCache;     // AT+ICCID
extern String modemModelCache;     // ATI 第二行（型号）
extern String modemFwCache;        // ATI 第三行（固件版本）

#endif
