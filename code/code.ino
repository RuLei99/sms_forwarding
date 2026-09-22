#include "globals.h"
#include "wifi_config.h"
#include "config.h"
#include "web_handlers.h"
#include "modem.h"
#include "push.h"
#include "sms_process.h"
#include "health.h"
#include "records.h"
#include "jobs.h"
#include <esp_task_wdt.h>
#include <LittleFS.h>

// 看门狗：loop 卡死（模组 AT 死等/SSL 挂起等）超过 30 秒自动复位，
// 复位原因会记录在启动日志里（esp_reset_reason）
static void watchdogInit() {
  esp_task_wdt_config_t cfg = {
    .timeout_ms = 30000,   // 覆盖大多数合法长操作；长等待循环内部会周期喂狗
    .idle_core_mask = 0,   // 不监控 idle 任务
    .trigger_panic = true  // 超时触发 panic → 复位（而非静默挂起）
  };
  esp_task_wdt_reconfigure(&cfg);  // 核心已初始化 TWDT，重配参数即可
  esp_task_wdt_add(NULL);          // 订阅当前任务（loopTask）
  logCaptureLn(String("看门狗已启用（30 秒）"));
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200);
  // 记录上次复位原因，便于排查异常重启（看门狗/掉电/软件重启）
  {
    const char* reason = "";
    switch (esp_reset_reason()) {
      case ESP_RST_POWERON: reason = "上电"; break;
      case ESP_RST_SW: reason = "软件重启"; break;
      case ESP_RST_PANIC: reason = "异常/ Panic"; break;
      case ESP_RST_INT_WDT: reason = "中断看门狗"; break;
      case ESP_RST_TASK_WDT: reason = "任务看门狗"; break;
      case ESP_RST_WDT: reason = "其他看门狗"; break;
      case ESP_RST_BROWNOUT: reason = "掉电检测"; break;
      case ESP_RST_DEEPSLEEP: reason = "深度睡眠唤醒"; break;
      default: reason = "其他"; break;
    }
    Serial.printf("[SYS] 启动，复位原因: %s\n", reason);
  }
  // 缩短初始化延时，WiFi连接会处理自己的超时
  delay(200);
  // 提醒用户 wifi_config.h 还是占位符（编译能过但必然连不上）
  if ((String(WIFI_SSID).length() == 0 || String(WIFI_SSID).indexOf("你家") >= 0)) {
    Serial.println("[SYS] ⚠️ wifi_config.h 中 WiFi SSID 仍是占位符");
    Serial.println("[SYS] 可先烧录固件后，在「网络测试」页配置主 WiFi（保存在设备里，无需重新编译）");
  }
  // RX 缓冲必须在 begin 之前设置：转发推送/邮件期间 loop 阻塞可达数十秒，
  // 500 字节缓冲会被并发到达的短信 PDU（约 340B/条）挤爆丢字节
  Serial1.setRxBufferSize(2048);
  Serial1.begin(115200, SERIAL_8N1, RXD, TXD);
  while (Serial1.available()) Serial1.read();
  modemPowerCycle();
  while (Serial1.available()) Serial1.read();
  initConcatBuffer();
  loadConfig();
  configValid = isConfigValid();

  // 时区（每日报告触发时间用；POSIX TZ 符号与实际相反：UTC-8 = UTC+8）
  {
    char tzBuf[16];
    snprintf(tzBuf, sizeof(tzBuf), "UTC%d", -config.tzHours);
    setenv("TZ", tzBuf, 1);
    tzset();
  }

  // 短信记录持久化（LittleFS，重启不丢）
  if (LittleFS.begin(true)) {
    recordsLoad();
    logCaptureLn(String("记录持久化已启用（LittleFS）"));
  } else {
    logCaptureLn(String("⚠️ LittleFS 挂载失败，短信记录仅存内存"));
  }

  // ---- WiFi 连接优化 ----
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                    // 关闭 Modem Sleep，提高连接响应速度
  WiFi.setAutoReconnect(true);             // 断线后自动重连
  // 使用快速扫描而非全信道扫描（全信道扫描在空信道上等待超时极慢）
  // 首次连接成功后 ESP32 会自动记住信道，下次启动更快
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  const char* primSsid; const char* primPass;
  getPrimaryWifi(primSsid, primPass);
  WiFi.begin(primSsid, primPass);
  logCaptureLn(String("连接wifi: ") + String(primSsid) +
               (config.wifi1Ssid.length() > 0 ? "（网页配置）" : "（固件内置）"));

  // 带超时的等待连接。主 WiFi 失败后尝试备用（双 WiFi 热备）。
  // 连续 3 次开机都失败则不再重启（避免无限重启循环），进入离线模式靠 autoReconnect 后台重连
  static RTC_DATA_ATTR int wifiBootFails = 0;  // RTC 内存：软重启后保留
  const unsigned long WIFI_TIMEOUT = 20000; // 单个 SSID 20秒超时
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < WIFI_TIMEOUT) {
    blink_short(200);
  }
  if (WiFi.status() != WL_CONNECTED && config.wifi2Ssid.length() > 0) {
    logCaptureLn(String("主 WiFi 未连接，尝试备用: " + config.wifi2Ssid));
    WiFi.begin(config.wifi2Ssid.c_str(), config.wifi2Pass.c_str());
    wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < WIFI_TIMEOUT) {
      blink_short(200);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiBootFails = 0;
    logCaptureLn(String("wifi已连接"));
    logCapture(String("IP地址: "));
    logCaptureLn(WiFi.localIP().toString());
    logCapture(String("信号强度(RSSI): "));
    logCaptureLn(String(WiFi.RSSI()) + " dBm");
  } else if (wifiBootFails < 3) {
    wifiBootFails++;
    logCaptureLn(String("⚠️ WiFi连接超时（第 ") + String(wifiBootFails) + "/3 次），即将重启重试...");
    delay(1000);
    ESP.restart();
  } else {
    logCaptureLn(String("⚠️ WiFi多次连接失败，进入离线模式（后台自动重连）。短信转发恢复需等 WiFi 就绪。"));
  }

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/tools", handleRoot);
  server.on("/sms", handleRoot);
  server.on("/sendsms", HTTP_POST, handleSendSms);
  server.on("/ping", HTTP_POST, handlePing);
  server.on("/flight", handleFlightMode);
  server.on("/at", handleATCommand);
  server.on("/log", handleLog);
  server.on("/modem", handleModem);
  server.on("/wifi", handleWifi);
  server.on("/system", handleSystem);
  server.on("/datalock", handleDataLock);
  server.on("/status", handleStatus);
  server.on("/smslog", handleSmsLog);
  server.on("/testpush", handleTestPush);
  server.on("/job", handleJobStatus);
  server.on("/recordsexport", handleRecordsExport);
  server.on("/config/export", handleConfigExport);
  server.on("/config/import", HTTP_POST, handleConfigImport);
  server.begin();
  logCaptureLn(String("HTTP服务器已启动"));

  // WiFi 断连/重连事件落日志（healthTask 消费）
  healthInit();

  // ---- NTP 时间同步 ----
  logCaptureLn(String("正在同步NTP时间..."));
  configTime(0, 0, "ntp.ntsc.ac.cn", "ntp.aliyun.com", "pool.ntp.org");
  int ntpRetry = 0;
  while (time(nullptr) < 100000 && ntpRetry < 100) {
    delay(1);
    server.handleClient();
    ntpRetry++;
  }
  if (time(nullptr) >= 100000) {
    timeSynced = true;
    logCaptureLn(String("NTP时间同步成功"));
    time_t now = time(nullptr);
    logCapture(String("当前UTC时间戳: "));
    logCaptureLn(String(now));
  } else {
    logCaptureLn(String("NTP时间同步失败，将使用设备时间"));
  }

  ssl_client.setInsecure();
  digitalWrite(LED_BUILTIN, LOW);

  // ---- 启动通知（网页已可用，发邮件不会影响用户访问） ----
  if (configValid) {
    logCaptureLn(String("配置有效，发送启动通知..."));
    String subject = "短信转发器已启动";
    String body = "设备已启动\n设备地址: " + getDeviceUrl();
    sendEmailNotification(subject.c_str(), body.c_str());
  }

  // ---- 模组初始化（较慢，但网页已可访问；失败不再死等，后台周期重试） ----
  if (!modemInit()) {
    logCaptureLn(String("⚠️ 模组初始化未完成，网页可用，后台每 2 分钟自动重试"));
  }

  // 看门狗最后启用：setup 中的长阻塞（模组初始化等）不受其约束
  watchdogInit();
}

void loop() {
  esp_task_wdt_reset();  // 每圈喂狗；长阻塞发生在各模块的等待循环内部，那里也有喂狗点
  server.handleClient();
  if (!configValid) {
    if (millis() - lastPrintTime >= 1000) {
      lastPrintTime = millis();
      logCaptureLn(String("⚠️ 请访问 " + getDeviceUrl() + " 配置系统参数"));
    }
  }
  checkConcatTimeout();
  // USB 串口透传给模组（调试用）：模组串口被长 AT 操作占用时跳过，
  // 否则透传字节会插进进行中的指令序列造成响应错乱
  if (Serial.available() && !modemPortBusy) Serial1.write(Serial.read());
  checkSerial1URC();
  // 模组任务队列（Ping/发短信/AT/重启 在这里执行，Web 端轮询结果）
  jobsRun();
  // 模组巡检 / 后台重初始化 / WiFi 事件日志 / 堆内存水位
  healthTask();
}
