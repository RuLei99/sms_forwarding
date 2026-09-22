#include "health.h"
#include "web_handlers.h"
#include "modem.h"
#include "jobs.h"
#include "push.h"
#include "wifi_config.h"
#include "config.h"

String modemSignalCache = "";
HealthDayStats dayStats = {0, 0, 0, 0, 0, 0, 0xFFFFFFFF};

// WiFi 事件回调运行在 WiFi 任务上下文，不能直接写日志环形缓冲（非线程安全），
// 只置标志，由 healthTask 在主循环里落日志
static volatile bool wfDisconnected = false;
static volatile bool wfGotIp = false;
static unsigned long wifiDownSince = 0;  // WiFi 掉线起始时刻（0=在线）

static void onWifiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: wfDisconnected = true; break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:       wfGotIp = true; break;
    default: break;
  }
}

void healthInit() {
  WiFi.onEvent(onWifiEvent);
}

// 巡检节奏
static const unsigned long CHECK_MS  = 300000UL;  // 模组巡检间隔 5 分钟
static const unsigned long REINIT_MS = 120000UL;  // 模组未就绪时后台重试间隔 2 分钟
static const unsigned long HEAP_MS   = 600000UL;  // 堆内存水位日志间隔 10 分钟

void healthTask() {
  unsigned long now = millis();

  // ---- WiFi 事件日志 ----
  if (wfGotIp) {
    wfGotIp = false;
    wifiDownSince = 0;
    logCaptureLn(String("WiFi 已连接: " + WiFi.localIP().toString()));
  }
  if (wfDisconnected) {
    wfDisconnected = false;
    wifiDownSince = now;
    logCaptureLn(String("WiFi 断开，自动重连中..."));
  }

  // ---- 双 WiFi 热备：掉线超过 60 秒切到另一个 SSID（autoReconnect 只重试同一 AP） ----
  static bool onBackupWifi = false;
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDownSince == 0) wifiDownSince = now;
    if (config.wifi2Ssid.length() > 0 && now - wifiDownSince >= 60000UL) {
      onBackupWifi = !onBackupWifi;
      const char* ssid; const char* pass;
      if (onBackupWifi) {
        ssid = config.wifi2Ssid.c_str();
        pass = config.wifi2Pass.c_str();
      } else {
        getPrimaryWifi(ssid, pass);
      }
      logCaptureLn(String("WiFi 持续掉线，切换到: " + String(ssid)));
      WiFi.begin(ssid, pass);
      wifiDownSince = now;  // 重置计时，再给 60 秒
    }
  } else {
    wifiDownSince = 0;
  }

  // ---- 堆内存水位 ----
  static unsigned long lastHeapLog = 0;
  if (now - lastHeapLog >= HEAP_MS) {
    lastHeapLog = now;
    logCaptureF("[SYS] heap=%uKB maxAlloc=%uKB\n",
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
    if (ESP.getFreeHeap() < dayStats.heapMin) dayStats.heapMin = ESP.getFreeHeap();
    if (ESP.getFreeHeap() < 60000) {
      logCaptureLn(String("⚠️ 堆内存偏低（<60KB），如持续出现请重启设备"));
    }
  }

  // ---- 模组巡检 / 后台重初始化 ----
  // 注：不使用 web_handlers 的 acquireModemPort（那是 HTTP 上下文），
  // 这里直接判 modemPortBusy，忙则本轮跳过（web 长操作优先）
  static unsigned long lastCheck = 0;
  static unsigned long lastReinit = 0;
  static int checkFails = 0;

  if (modemReady && now - lastCheck >= CHECK_MS && !modemPortBusy && jobsIdle()) {
    lastCheck = now;
    modemPortBusy = true;
    String resp = sendATCommand("AT+CEREG?", 3000);
    bool reg = (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
    // 顺带刷新信号缓存（同一次串口占用内）
    if (reg) {
      int dbm;
      if (modemParseCsq(sendATCommand("AT+CSQ", 2000), dbm)) {
        modemSignalCache = String(dbm) + " dBm";
        if (dayStats.sigMin == 0 || dbm < dayStats.sigMin) dayStats.sigMin = dbm;
        if (dbm > dayStats.sigMax) dayStats.sigMax = dbm;
      } else {
        modemSignalCache = "未知";
      }
      // 运营商随漫游/换卡变化，巡检顺带刷新（同一次串口占用内）；解析失败保留旧值
      String op = modemParseCops(sendATCommand("AT+COPS?", 2000));
      if (op.length() > 0) modemOperatorCache = op;
    }
    modemPortBusy = false;

    if (reg) {
      if (checkFails > 0) logCaptureLn(String("[巡检] 模组恢复正常"));
      checkFails = 0;
    } else {
      checkFails++;
      logCaptureLn(String("[巡检] 模组无响应/未注册（第 " + String(checkFails) + " 次）"));
      if (checkFails >= 2) {
        checkFails = 0;
        modemReady = false;
        logCaptureLn(String("⚠️ 连续巡检失败，对模组断电重启"));
        resetModule();          // EN 断电 + 重新初始化（modemInit 内部已封顶重试）
        lastReinit = millis();  // 失败则按 REINIT_MS 节奏由下面继续重试
      }
    }
  }

  if (!modemReady && now - lastReinit >= REINIT_MS) {
    lastReinit = now;
    logCaptureLn(String("[后台] 重新初始化模组..."));
    modemInit();
  }

  // ---- NTP 后台补同步 ----
  // setup 里只等 100ms，路由器刚上电时几乎必然失败；这里每 10 分钟重试直到成功
  static unsigned long lastNtpTry = 0;
  if (time(nullptr) < 100000 && now - lastNtpTry >= 600000UL) {
    lastNtpTry = now;
    configTime(0, 0, "ntp.ntsc.ac.cn", "ntp.aliyun.com", "pool.ntp.org");
    logCaptureLn(String("[后台] 重试 NTP 时间同步..."));
  }
  if (!timeSynced && time(nullptr) >= 100000) {
    timeSynced = true;
    logCaptureLn(String("NTP时间同步成功（后台补同步）"));
  }

  // ---- 每日健康报告（本地时区 8 点，每天一次） ----
  static int lastReportDay = -1;
  if (config.reportEnabled && timeSynced) {
    time_t nowT = time(nullptr);
    struct tm lt;
    localtime_r(&nowT, &lt);
    if (lt.tm_hour == 8 && lt.tm_yday != lastReportDay) {
      lastReportDay = lt.tm_yday;
      logCaptureLn(String("触发每日健康报告"));
      healthSendReport();
    }
  }
}

void healthSendReport() {
  char dateBuf[24];
  time_t nowT = time(nullptr);
  if (nowT < 100000) {
    snprintf(dateBuf, sizeof(dateBuf), "(time unsynced)");
  } else {
    struct tm lt;
    localtime_r(&nowT, &lt);
    strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &lt);
  }

  String NL2 = "\n";
  String text = "短信转发器每日报告 ";
  text += dateBuf;
  text += NL2 + "----------" + NL2;
  text += "收到短信: " + String(dayStats.smsIn) + " 条";
  if (dayStats.blocked > 0) text += "（拦截 " + String(dayStats.blocked) + " 条）";
  text += NL2 + "转发成功: " + String(dayStats.fwdOk) + " 条";
  if (dayStats.fwdFail > 0) text += NL2 + "转发失败: " + String(dayStats.fwdFail) + " 条";
  if (dayStats.sigMin != 0) {
    text += NL2 + "信号范围: " + String(dayStats.sigMin) + " ~ " + String(dayStats.sigMax) + " dBm";
  }
  if (dayStats.heapMin != 0xFFFFFFFF) {
    text += NL2 + "堆内存最低: " + String(dayStats.heapMin / 1024) + " KB";
  }
  text += NL2 + "运行时长: " + String(millis() / 3600000) + " 小时";

  pushBroadcastText("短信转发器每日健康报告", text.c_str());

  dayStats.smsIn = dayStats.fwdOk = dayStats.fwdFail = dayStats.blocked = 0;
  dayStats.sigMin = dayStats.sigMax = 0;
  dayStats.heapMin = 0xFFFFFFFF;
}
