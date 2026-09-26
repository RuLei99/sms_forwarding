#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <pdulib.h>
#define ENABLE_SMTP
#define ENABLE_DEBUG
#include <ReadyMail.h>
#include "config_types.h"
#include <esp_task_wdt.h>

// 串口映射
#define TXD 3
#define RXD 4
#define MODEM_EN_PIN 5

// LED引脚定义（用于通过CI验证，给个假的）
#ifndef LED_BUILTIN
#define LED_BUILTIN 8
#endif

// 一条 PDU/URC 行的接收上限。ML307R 在部分固件下会输出较长的 CMGR/CMT 行，
// 500 字节容易把合法短信误判为溢出；1KB 仍远小于 pdulib 的 4KB 解码缓冲。
#define SERIAL_BUFFER_SIZE 1024
#define MAX_PDU_LENGTH 300

// 全局变量声明
extern Config config;
extern Preferences preferences;
extern PDU pdu;
extern WiFiClientSecure ssl_client;
extern SMTPClient smtp;
extern WebServer server;
extern bool configValid;
extern bool timeSynced;
extern bool modemReady;
extern bool modemPortBusy;   // Serial1 互斥：长 AT 操作期间拒绝其他模组请求，防止嵌套 handleClient 抢读串口
extern bool wdtArmed;        // 看门狗是否已启用（setup 末尾）；启用前喂狗调用会被跳过
extern bool simPinUnlockFailed;  // 本次开机内 PIN 解锁已失败过；置位后 health 周期重试不再尝试（防连续错 3 次锁 PUK），网页重新保存 PIN 时清除

// 统一喂狗入口：WDT 未启用前调用是无效操作且会刷错误日志，这里统一守卫
static inline void wdtFeed() { if (wdtArmed) esp_task_wdt_reset(); }
extern unsigned long lastPrintTime;
extern ConcatSms concatBuffer[MAX_CONCAT_MESSAGES];

#endif
