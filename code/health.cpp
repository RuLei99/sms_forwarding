#include "health.h"
#include "web_handlers.h"
#include "modem.h"
#include "jobs.h"
#include "push.h"
#include "wifi_config.h"
#include "config.h"

String modemSignalCache = "";

// WiFi 事件回调运行在 WiFi 任务上下文，不能直接写日志环形缓冲（非线程安全），
// 只置标志，由 healthTask 在主循环里落日志
static volatile bool wfDisconnected = false;
static volatile bool wfGotIp = false;
static volatile uint8_t wfDisconnectReason = 0;
static unsigned long wifiDownSince = 0;  // WiFi 掉线起始时刻（0=在线）

static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      wfDisconnectReason = info.wifi_sta_disconnected.reason;
      wfDisconnected = true;
      break;
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
    // 只在「在线→掉线」跳变时记录时刻：autoReconnect 重试风暴会连续发断开事件，
    // 反复重置 wifiDownSince 会让 60 秒切网/10 秒主动重连永远不触发（实测踩坑）
    if (wifiDownSince == 0) {
      wifiDownSince = now;
      logCaptureLn(String("WiFi 断开(reason=") +
                   WiFi.disconnectReasonName((wifi_err_reason_t)wfDisconnectReason) +
                   ")，自动重连中...");
    }
  }

  // ---- 双 WiFi 热备：掉线超过 60 秒切到另一个 SSID（autoReconnect 只重试同一 AP） ----
  static bool onBackupWifi = false;
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDownSince == 0) wifiDownSince = now;
    // autoReconnect 在 AUTH_EXPIRE/供电抖动后偶尔停留在断开态，主动踢一次状态机。
    static unsigned long lastReconnect = 0;
    if (now - wifiDownSince >= 10000UL && now - lastReconnect >= 10000UL) {
      lastReconnect = now;
      WiFi.reconnect();
      logCaptureLn(String("WiFi 主动重连..."));
    }
    const char* bakSsid = "";
    const char* bakPass = "";
    getBackupWifi(bakSsid, bakPass);
    if (strlen(bakSsid) > 0 && now - wifiDownSince >= 60000UL) {
      onBackupWifi = !onBackupWifi;
      const char* ssid; const char* pass;
      if (onBackupWifi) {
        getBackupWifi(ssid, pass);
      } else {
        getPrimaryWifi(ssid, pass);
      }
      logCaptureLn(String("WiFi 持续掉线，切换到: " + String(ssid)));
      // 完全停再重启 STA，避免状态机卡在 connecting/leaving 导致新配置进不去
      WiFi.mode(WIFI_OFF);
      delay(500);
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, pass);
      wifiDownSince = now;  // 重置计时，再给 60 秒
    }

    // 长时间两张网都连不上（在位切换失败/状态机卡死）：重启兜底。开机连接路径
    // 是唯一被实战验证可靠的切网方式；短信已走存储路由，重启不丢。
    // 门限：掉线 ≥5 分钟且开机已运行 ≥15 分钟——后者防止两张网全挂时无限重启
    // （开机阶段自身的 3 次重试失败会进离线模式，不会走到这里）
    if (now - wifiDownSince >= 300000UL && millis() >= 900000UL) {
      logCaptureLn(String("⚠️ WiFi 掉线超 5 分钟未恢复，重启设备兜底..."));
      delay(500);
      ESP.restart();
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
    // 纯 AT 重试救不回「失聪」的模组（供电/接触问题）：连续 3 次失败升级为
    // EN 断电重启，这是固件手里唯一的硬恢复手段
    static uint8_t reinitFails = 0;
    if (reinitFails >= 2) {
      reinitFails = 0;
      logCaptureLn(String("[后台] 连续初始化失败，EN 断电重启模组..."));
      resetModule();  // EN 断电 + 重新初始化
    } else {
      reinitFails++;
      logCaptureLn(String("[后台] 重新初始化模组（第 " + String(reinitFails) + "/2 次，再失败将断电重启）..."));
      modemInit();
    }
    if (modemReady) reinitFails = 0;
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

}
