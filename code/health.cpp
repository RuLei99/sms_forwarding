#include "health.h"
#include "web_handlers.h"
#include "modem.h"

String modemSignalCache = "";

// WiFi 事件回调运行在 WiFi 任务上下文，不能直接写日志环形缓冲（非线程安全），
// 只置标志，由 healthTask 在主循环里落日志
static volatile bool wfDisconnected = false;
static volatile bool wfGotIp = false;

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
    logCaptureLn(String("WiFi 已连接: " + WiFi.localIP().toString()));
  }
  if (wfDisconnected) {
    wfDisconnected = false;
    logCaptureLn(String("WiFi 断开，自动重连中..."));
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

  if (modemReady && now - lastCheck >= CHECK_MS && !modemPortBusy) {
    lastCheck = now;
    modemPortBusy = true;
    String resp = sendATCommand("AT+CEREG?", 3000);
    bool reg = (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0);
    // 顺带刷新信号缓存（同一次串口占用内）
    if (reg) {
      String csq = sendATCommand("AT+CSQ", 2000);
      int commaIdx = csq.indexOf(',');
      if (csq.indexOf("+CSQ:") >= 0 && commaIdx > 0) {
        int rssi = csq.substring(csq.indexOf(':') + 1, commaIdx).toInt();
        if (rssi != 99 && rssi >= 0) {
          int dbm = -113 + rssi * 2;
          modemSignalCache = String(dbm) + " dBm";
        } else {
          modemSignalCache = "未知";
        }
      }
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
}
