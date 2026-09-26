#include "web_handlers.h"
#include "web_html.h"
#include "config.h"
#include "modem.h"
#include "push.h"
#include "wifi_config.h"
#include "records.h"
#include <esp_task_wdt.h>
#include "health.h"
#include "jobs.h"
#include <LittleFS.h>

// ---- 日志环形缓冲区 ----
String logBuffer[LOG_BUF_SIZE];
int logBufIdx = 0;
int logBufCount = 0;
static String _logLine;  // 行缓冲：logCapture 写入这里，logCaptureLn 提交整行

static void _logAppend(const String& line) {
  // 截断防内存膨胀：PDU 调试行可达 700+ 字符，120 行环形缓冲全存长行时
  // /log 序列化峰值会冲到 200KB+ 堆直接 OOM；日志截断到 200 字符不影响排障
  if (line.length() > 200) {
    String t = line.substring(0, 197) + "...";
    logBuffer[logBufIdx] = t;
  } else {
    logBuffer[logBufIdx] = line;
  }
  logBufIdx = (logBufIdx + 1) % LOG_BUF_SIZE;
  if (logBufCount < LOG_BUF_SIZE) logBufCount++;
}

static void _logCommit() {
  if (_logLine.length() > 0) {
    _logAppend(_logLine);
    _logLine = "";
  }
}

void logCapture(const String& msg) {
  Serial.print(msg);
  _logLine += msg;
}

void logCapture(const char* msg) {
  Serial.print(msg);
  _logLine += msg;
}

void logCaptureF(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
  _logLine += buf;
  // 如果格式化字符串以 \n 结尾，则提交此行
  size_t len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n') {
    _logLine.trim();  // 去掉尾部空格和可能多余的 \n
    _logCommit();
  }
}

void logCaptureLn(const String& msg) {
  Serial.println(msg);
  _logLine += msg;
  _logCommit();
}

void logCaptureLn(const char* msg) {
  Serial.println(msg);
  _logLine += msg;
  _logCommit();
}

// 检查HTTP Basic认证
bool checkAuth() {
  if (!server.authenticate(config.webUser.c_str(), config.webPass.c_str())) {
    server.requestAuthentication(BASIC_AUTH, "SMS Forwarding", "请输入管理员账号密码");
    return false;
  }
  return true;
}

// ---- 模组串口互斥 ----
// 长 AT 操作（Ping/重启/发短信等）等待期间会嵌套调用 server.handleClient()，
// 若此时另一个请求也去读写 Serial1，双方响应会互相截断。
// 所有会占用 Serial1 的处理器进入前必须 acquire，用完 release。
static bool acquireModemPort(const char* who) {
  if (modemPortBusy) {
    logCaptureLn(String("模组串口忙，拒绝请求: ") + String(who));
    server.send(429, "application/json", "{\"success\":false,\"message\":\"模组正忙，请稍后重试\"}");
    return false;
  }
  modemPortBusy = true;
  return true;
}
static void releaseModemPort() { modemPortBusy = false; }

// ---- 页面流式渲染 ----
// 直接遍历 flash 中的页面字面量，遇到 %KEY%（大写字母/下划线）查表替换，
// 以 1KB 块下发。避免"整页拷入堆 + 逐个 replace 再整体复制"的内存开销
// （52KB 页面峰值会占到 100KB+ 堆，挤压 WiFi/SSL/SMTP）。
struct PageVar { const char* key; String value; };

// 配置内容会进入 input value / textarea /普通文本。统一转义，避免引号破坏页面，
// 也避免通过配置导入把 HTML/脚本注入管理页。
static String htmlEscape(const String& input) {
  String out;
  out.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); i++) {
    switch (input[i]) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += input[i]; break;
    }
  }
  return out;
}

static void streamTemplatedPage(const char* page, const PageVar* vars, int nVars) {
  char chunk[1024];
  size_t len = 0;
  auto flush = [&]() {
    if (len > 0) {
      server.sendContent(String(chunk, len));
      len = 0;
    }
  };

  // Content-Type 必须带 charset=utf-8：部分内嵌浏览器不解析 <meta charset>，会按错误编码显示中文
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

  const char* p = page;
  while (*p) {
    if (*p == '%') {
      const char* e = p + 1;
      while (*e == '_' || (*e >= 'A' && *e <= 'Z')) e++;
      size_t klen = e - p - 1;
      bool matched = false;
      if (klen >= 1 && klen <= 24 && *e == '%') {
        char key[25];
        memcpy(key, p + 1, klen);
        key[klen] = 0;
        for (int i = 0; i < nVars; i++) {
          if (strcmp(vars[i].key, key) == 0) {
            flush();
            // 空值绝不能 sendContent：chunked 模式下零长度块 = 响应终止符，
            // 会把页面拦腰截断（首次配置时空 ADMIN_PHONE/SMTP_* 必现）
            if (vars[i].value.length() > 0) server.sendContent(vars[i].value);
            p = e + 1;
            matched = true;
            break;
          }
        }
      }
      if (matched) continue;  // 已替换，处理下一字符
    }
    // 普通字符（含未命中的 %，如 CSS 中的百分号）原样进块缓冲
    chunk[len++] = *p++;
    if (len >= sizeof(chunk)) flush();
  }
  flush();
  server.sendContent("");  // 结束 chunked 响应
}

// 处理配置页面请求
void handleRoot() {
  if (!checkAuth()) return;

  // 禁用缓存：固件更新后确保浏览器拉取新版页面，避免旧版 UI 与新固件不匹配
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  char uptimeBuf[16];
  long uptimeSec = millis() / 1000;
  snprintf(uptimeBuf, sizeof(uptimeBuf), "%ld:%02ld:%02ld", uptimeSec / 3600, (uptimeSec % 3600) / 60, uptimeSec % 60);

  // 概览页面的配置状态
  bool emailOk = config.smtpServer.length() > 0 && config.smtpUser.length() > 0 &&
                 config.smtpPass.length() > 0 && config.smtpSendTo.length() > 0;
  int pushCount = 0;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (config.pushChannels[i].enabled) pushCount++;
  }

  // 生成推送通道HTML
  String channelsHtml = "";
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    String idx = String(i);
    String enabledClass = config.pushChannels[i].enabled ? " enabled" : "";
    String checked = config.pushChannels[i].enabled ? " checked" : "";

    channelsHtml += "<div class=\"push-channel" + enabledClass + "\" id=\"channel" + idx + "\">";
    channelsHtml += "<div class=\"push-channel-header\">";
    channelsHtml += "<input type=\"checkbox\" name=\"push" + idx + "en\" id=\"push" + idx + "en\" onchange=\"toggleChannel(" + idx + ")\"" + checked + ">";
    channelsHtml += "<label for=\"push" + idx + "en\" class=\"label-inline\">启用推送通道 " + String(i + 1) + "</label>";
    channelsHtml += "<button type=\"button\" class=\"btn btn-sm btn-secondary ch-test\" onclick=\"testPush(" + idx + ")\" id=\"testBtn" + idx + "\">发送测试</button>";
    channelsHtml += "</div>";
    channelsHtml += "<div class=\"push-channel-body\">";

    // 通道名称
    channelsHtml += "<div class=\"form-group\">";
    channelsHtml += "<label>通道名称</label>";
    channelsHtml += "<input type=\"text\" name=\"push" + idx + "name\" value=\"" + htmlEscape(config.pushChannels[i].name) + "\" placeholder=\"自定义名称\">";
    channelsHtml += "</div>";

    // 推送类型
    channelsHtml += "<div class=\"form-group\">";
    channelsHtml += "<label>推送方式</label>";
    channelsHtml += "<select name=\"push" + idx + "type\" id=\"push" + idx + "type\" onchange=\"updateTypeHint(" + idx + ")\">";
    for (size_t ti = 0; ti < pushTypeCount(); ti++) {
      PushType tt = (PushType)(ti + 1);
      channelsHtml += "<option value=\"" + String((int)tt) + "\"" +
                      String(config.pushChannels[i].type == tt ? " selected" : "") + ">" + pushTypeName(tt) + "</option>";
    }
    channelsHtml += "</select>";
    channelsHtml += "<div class=\"push-type-hint\" id=\"hint" + idx + "\"></div>";
    channelsHtml += "</div>";

    // URL
    channelsHtml += "<div class=\"form-group\">";
    channelsHtml += "<label>推送URL/Webhook</label>";
    channelsHtml += "<input type=\"text\" name=\"push" + idx + "url\" id=\"url" + idx + "\" value=\"" + htmlEscape(config.pushChannels[i].url) + "\" placeholder=\"http://your-server.com/api 或 webhook地址\">";
    channelsHtml += "</div>";

    // 额外参数区域（钉钉/PushPlus/Server酱等需要）
    channelsHtml += "<div id=\"extra" + idx + "\" style=\"display:none;\">";
    channelsHtml += "<div class=\"form-group\">";
    channelsHtml += "<label id=\"key1label" + idx + "\">参数1</label>";
    channelsHtml += "<input type=\"text\" name=\"push" + idx + "key1\" id=\"key1" + idx + "\" value=\"" + htmlEscape(config.pushChannels[i].key1) + "\">";
    channelsHtml += "</div>";
    channelsHtml += "<div class=\"form-group\" id=\"key2group" + idx + "\">";
    channelsHtml += "<label id=\"key2label" + idx + "\">参数2</label>";
    channelsHtml += "<input type=\"text\" name=\"push" + idx + "key2\" id=\"key2" + idx + "\" value=\"" + htmlEscape(config.pushChannels[i].key2) + "\">";
    channelsHtml += "</div>";
    channelsHtml += "</div>";

    // 自定义模板区域
    channelsHtml += "<div id=\"custom" + idx + "\" style=\"display:none;\">";
    channelsHtml += "<div class=\"form-group\">";
    channelsHtml += "<label>请求体模板（使用 {sender} {message} {timestamp} 占位符）</label>";
    channelsHtml += "<textarea name=\"push" + idx + "body\" rows=\"4\" style=\"width:100%;font-family:monospace;\">" + htmlEscape(config.pushChannels[i].customBody) + "</textarea>";
    channelsHtml += "</div>";
    channelsHtml += "</div>";

    channelsHtml += "</div></div>";
  }

  PageVar vars[] = {
    {"IP", WiFi.localIP().toString()},
    {"WIFI_SSID", String(WiFi.SSID())},
    {"FREE_HEAP", String(ESP.getFreeHeap() / 1024) + " KB"},
    {"UPTIME", String(uptimeBuf)},
    {"WEB_USER", htmlEscape(config.webUser)},
    {"WEB_PASS", htmlEscape(config.webPass)},
    {"SMTP_SERVER", htmlEscape(config.smtpServer)},
    {"SMTP_PORT", String(config.smtpPort)},
    {"SMTP_USER", htmlEscape(config.smtpUser)},
    {"SMTP_PASS", htmlEscape(config.smtpPass)},
    {"SMTP_SEND_TO", htmlEscape(config.smtpSendTo)},
    {"ADMIN_PHONE", htmlEscape(config.adminPhone)},
    {"NUMBER_BLACK_LIST", htmlEscape(config.numberBlackList)},
    {"FILTER_KEYWORDS", htmlEscape(config.filterKeywords)},
    {"FLT_WL_SEL", config.filterWhitelist ? " selected" : ""},
    {"FLT_BL_SEL", config.filterWhitelist ? "" : " selected"},
    {"WIFI1_SSID", htmlEscape(config.wifi1Ssid)},
    {"WIFI1_PASS", htmlEscape(config.wifi1Pass)},
    {"WIFI2_SSID", htmlEscape(config.wifi2Ssid)},
    {"WIFI2_PASS", htmlEscape(config.wifi2Pass)},
    {"SIM_PIN", htmlEscape(config.simPin)},
    {"SMTP_CHECK", emailOk ? "已配置" : "未配置"},
    {"MODEM_CHECK", modemReady ? "已就绪" : "未就绪"},
    {"PUSH_COUNT", String(pushCount)},
    {"PUSH_CHANNELS", channelsHtml},
  };
  streamTemplatedPage(htmlPage, vars, sizeof(vars) / sizeof(vars[0]));
}

// 处理工具箱页面请求 — 已整合到主页，直接返回主页
void handleToolsPage() {
  handleRoot();
}

// 处理飞行模式控制请求
void handleFlightMode() {
  if (!checkAuth()) return;

  String action = server.arg("action");
  String json = "{";
  bool success = false;
  String message = "";

  if (action == "query") {
    // 查询当前功能模式
    if (!acquireModemPort("/flight query")) return;
    logCaptureLn(String("网页端查询飞行模式: AT+CFUN?"));
    String resp = sendATCommand("AT+CFUN?", 2000);
    logCaptureLn(String("CFUN查询响应: " + resp));
    releaseModemPort();

    if (resp.indexOf("+CFUN:") >= 0) {
      success = true;
      int idx = resp.indexOf("+CFUN:");
      int mode = resp.substring(idx + 6).toInt();

      String modeStr;
      if (mode == 0) {
        modeStr = "最小功能模式（关机）";
      } else if (mode == 1) {
        modeStr = "全功能模式（正常）";
      } else if (mode == 4) {
        modeStr = "飞行模式（射频关闭）";
      } else {
        modeStr = "未知模式 (" + String(mode) + ")";
      }

      message = "<table class='info-table'>";
      message += "<tr><td>当前状态</td><td>" + modeStr + "</td></tr>";
      message += "<tr><td>CFUN值</td><td>" + String(mode) + "</td></tr>";
      message += "</table>";
    } else {
      message = "查询失败";
    }
  }
  else if (action == "toggle") {
    // 先查询当前状态
    if (!acquireModemPort("/flight toggle")) return;
    String resp = sendATCommand("AT+CFUN?", 2000);
    logCaptureLn(String("CFUN查询响应: " + resp));

    if (resp.indexOf("+CFUN:") >= 0) {
      int idx = resp.indexOf("+CFUN:");
      int currentMode = resp.substring(idx + 6).toInt();

      // 切换模式：1(正常) <-> 4(飞行模式)
      int newMode = (currentMode == 1) ? 4 : 1;
      String cmd = "AT+CFUN=" + String(newMode);

      logCaptureLn(String("切换飞行模式: " + cmd));
      String setResp = sendATCommand(cmd.c_str(), 5000);
      logCaptureLn(String("CFUN设置响应: " + setResp));
      releaseModemPort();

      if (setResp.indexOf("OK") >= 0) {
        success = true;
        if (newMode == 4) {
          message = "已开启飞行模式<br>模组射频已关闭，无法收发短信";
        } else {
          message = "已关闭飞行模式<br>模组恢复正常工作";
        }
      } else {
        message = "切换失败: " + setResp;
      }
    } else {
      releaseModemPort();
      message = "无法获取当前状态";
    }
  }
  else if (action == "on") {
    // 强制开启飞行模式
    if (!acquireModemPort("/flight on")) return;
    logCaptureLn(String("网页端强制开启飞行模式: AT+CFUN=4"));
    String resp = sendATCommand("AT+CFUN=4", 5000);
    releaseModemPort();
    if (resp.indexOf("OK") >= 0) {
      success = true;
      message = "已开启飞行模式";
    } else {
      message = "开启失败: " + resp;
    }
  }
  else if (action == "off") {
    // 强制关闭飞行模式
    if (!acquireModemPort("/flight off")) return;
    logCaptureLn(String("网页端关闭飞行模式: AT+CFUN=1"));
    String resp = sendATCommand("AT+CFUN=1", 5000);
    releaseModemPort();
    if (resp.indexOf("OK") >= 0) {
      success = true;
      message = "已关闭飞行模式";
    } else {
      message = "关闭失败: " + resp;
    }
  }
  else {
    message = "未知操作";
  }

  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

// 处理AT指令测试请求
void handleATCommand() {
  if (!checkAuth()) return;

  String cmd = server.arg("cmd");
  if (cmd.length() == 0) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"错误：指令不能为空\"}");
    return;
  }
  uint32_t id = jobsSubmit(MODEM_JOB_AT, cmd.c_str());
  if (id == 0) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"任务队列忙，请稍后重试\"}");
    return;
  }
  logCaptureLn(String("AT 指令已入队（任务" + String(id) + "）: " + cmd));
  server.send(200, "application/json",
              ("{\"queued\":true,\"id\":" + String(id) + ",\"message\":\"指令已加入队列\"}").c_str());
}

// 任务状态轮询：/job?id=N → queued/running/done
void handleJobStatus() {
  if (!checkAuth()) return;
  uint32_t id = (uint32_t)server.arg("id").toInt();
  ModemResult r;
  int st = jobsQuery(id, r);
  String json = "{";
  if (st == 0)      json += "\"state\":\"queued\",\"success\":false,\"message\":\"排队中...\"";
  else if (st == 1) json += "\"state\":\"running\",\"success\":false,\"message\":\"执行中...\"";
  else if (st == 2) json += String("\"state\":\"done\",\"success\":") + (r.success ? "true" : "false") +
                            ",\"message\":\"" + jsonEscape(String(r.message)) + "\"";
  else              json += "\"state\":\"unknown\",\"success\":false,\"message\":\"任务不存在或结果已过期\"";
  json += "}";
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "application/json", json);
}

// 处理发送短信请求
void handleSendSms() {
  if (!checkAuth()) return;

  String phone = server.arg("phone");
  String content = server.arg("content");
  phone.trim();
  content.trim();

  if (phone.length() == 0 || content.length() == 0) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"请填写目标号码和短信内容\"}");
    return;
  }
  if (content.length() >= MODEM_JOB_ARG2_SIZE) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"内容过长（超长短信暂不支持网页发送）\"}");
    return;
  }
  uint32_t id = jobsSubmit(MODEM_JOB_SEND_SMS, phone.c_str(), content.c_str());
  if (id == 0) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"任务队列忙，请稍后重试\"}");
    return;
  }
  logCaptureLn(String("网页发短信已入队（任务" + String(id) + "）→ " + phone));
  server.send(200, "application/json",
              ("{\"queued\":true,\"id\":" + String(id) + ",\"message\":\"短信已加入发送队列\"}").c_str());
}

// 处理Ping请求
void handlePing() {
  if (!checkAuth()) return;

  uint32_t id = jobsSubmit(MODEM_JOB_PING);
  if (id == 0) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"任务队列忙，请稍后重试\"}");
    return;
  }
  server.send(200, "application/json",
              ("{\"queued\":true,\"id\":" + String(id) + ",\"message\":\"Ping 已加入队列（最长约 35 秒）\"}").c_str());
}

// 处理保存配置请求
void handleSave() {
  if (!checkAuth()) return;

  // 账号管理表单：只在字段存在时更新
  // 空用户名/密码不回退默认值而是保留旧值 —— 否则清空保存会静默把密码重置为弱默认密码
  if (server.hasArg("webUser")) {
    String newWebUser = server.arg("webUser");
    newWebUser.trim();
    if (newWebUser.length() > 0) config.webUser = newWebUser;
  }
  if (server.hasArg("webPass")) {
    String newWebPass = server.arg("webPass");
    if (newWebPass.length() > 0) config.webPass = newWebPass;
  }

  // 邮件通知表单：只在字段存在时更新
  if (server.hasArg("smtpServer")) {
    config.smtpServer = server.arg("smtpServer");
  }
  if (server.hasArg("smtpPort")) {
    config.smtpPort = server.arg("smtpPort").toInt();
    if (config.smtpPort <= 0 || config.smtpPort > 65535) config.smtpPort = DEFAULT_SMTP_PORT;
  }
  if (server.hasArg("smtpUser")) {
    config.smtpUser = server.arg("smtpUser");
  }
  if (server.hasArg("smtpPass")) {
    config.smtpPass = server.arg("smtpPass");
  }
  if (server.hasArg("smtpSendTo")) {
    config.smtpSendTo = server.arg("smtpSendTo");
  }

  // 管理员 & 黑名单表单：只在字段存在时更新
  if (server.hasArg("adminPhone")) {
    config.adminPhone = server.arg("adminPhone");
  }
  if (server.hasArg("numberBlackList")) {
    config.numberBlackList = server.arg("numberBlackList");
  }

  // 关键词过滤表单：只在字段存在时更新
  if (server.hasArg("filterKeywords")) {
    config.filterKeywords = server.arg("filterKeywords");
  }
  if (server.hasArg("filterMode")) {
    config.filterWhitelist = (server.arg("filterMode") == "whitelist");
  }

  // SIM PIN（模组控制表单）：留空 = 清除（不自动解锁）
  if (server.hasArg("simPin")) {
    String pin = server.arg("simPin");
    pin.trim();
    if (isSimPinValid(pin)) {
      config.simPin = pin;
      // 用户显式重新保存 PIN，解除“本次开机已解锁失败”锁存，允许再试一次
      simPinUnlockFailed = false;
    } else {
      logCaptureLn(String("⚠️ SIM PIN 非法（仅允许4-8位数字），忽略本次修改"));
    }
  }

  // 主 WiFi（网络测试表单）：留空 = 恢复使用固件内置 wifi_config.h
  if (server.hasArg("wifi1Ssid")) {
    config.wifi1Ssid = server.arg("wifi1Ssid");
    config.wifi1Ssid.trim();
    if (server.hasArg("wifi1Pass")) config.wifi1Pass = server.arg("wifi1Pass");
    if (config.wifi1Ssid.length() == 0) config.wifi1Pass = "";
  }

  // 备用 WiFi（网络测试表单）
  if (server.hasArg("wifi2Ssid")) {
    config.wifi2Ssid = server.arg("wifi2Ssid");
    config.wifi2Ssid.trim();
    config.wifi2Pass = server.hasArg("wifi2Pass") ? server.arg("wifi2Pass") : config.wifi2Pass;
    if (config.wifi2Ssid.length() == 0) config.wifi2Pass = "";  // 关闭热备时清密码
  }

  // 推送通道配置：只在对应通道的字段存在时更新
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    String idx = String(i);
    String enKey = "push" + idx + "en";
    String typeKey = "push" + idx + "type";
    String urlKey = "push" + idx + "url";
    String nameKey = "push" + idx + "name";
    String k1Key = "push" + idx + "key1";
    String k2Key = "push" + idx + "key2";
    String bodyKey = "push" + idx + "body";
    // 只要该通道的任一字段存在，就更新整个通道
    if (server.hasArg(enKey) || server.hasArg(typeKey) || server.hasArg(urlKey) ||
        server.hasArg(nameKey) || server.hasArg(k1Key) || server.hasArg(k2Key) ||
        server.hasArg(bodyKey)) {
      config.pushChannels[i].enabled = server.arg(enKey) == "on";
      int rawType = server.arg(typeKey).toInt();
      // 表单外的非法类型值直接丢弃回退，避免脏 enum 存入 NVS 后推送异常
      if (rawType < PUSH_TYPE_MIN || rawType > PUSH_TYPE_MAX) rawType = PUSH_TYPE_POST_JSON;
      config.pushChannels[i].type = (PushType)rawType;
      config.pushChannels[i].url = server.arg(urlKey);
      config.pushChannels[i].name = server.arg(nameKey);
      config.pushChannels[i].key1 = server.arg(k1Key);
      config.pushChannels[i].key2 = server.arg(k2Key);
      config.pushChannels[i].customBody = server.arg(bodyKey);
      if (config.pushChannels[i].name.length() == 0) {
        config.pushChannels[i].name = "通道" + String(i + 1);
      }
    }
  }
  
  saveConfig();
  configValid = isConfigValid();

  // JSON 响应（前端 fetch 提交，行内反馈）
  String msg = "配置已保存";
  if (server.hasArg("webPass")) msg += "；账号密码已更新，下次登录请使用新凭据";
  if (!configValid) msg += "；提醒：邮件与推送通道均未配置，短信暂无法转发";
  String json = "{\"success\":true,\"message\":\"" + jsonEscape(msg) + "\"}";
  server.send(200, "application/json", json);
  
  // 如果配置有效，发送启动通知
  if (configValid) {
    logCaptureLn(String("配置有效，发送启动通知..."));
    String subject = "短信转发器配置已更新";
    String body = "设备配置已更新\n设备地址: " + getDeviceUrl();
    sendEmailNotification(subject.c_str(), body.c_str());
  }
}

// 处理日志查询请求 — 返回环形缓冲区中的日志行
void handleLog() {
  if (!checkAuth()) return;

  String json;
  json.reserve(4096 + logBufCount * 32);  // 减少多次 += 触发的 realloc/拷贝
  json = "[";
  int total = logBufCount;
  int start = total < LOG_BUF_SIZE ? 0 : logBufIdx;
  for (int i = 0; i < total; i++) {
    int pos = (start + i) % LOG_BUF_SIZE;
    if (i > 0) json += ",";
    json += "\"" + jsonEscape(logBuffer[pos]) + "\"";
  }
  json += "]";
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "application/json", json);
}

// 模组控制命令
void handleModem() {
  if (!checkAuth()) return;

  // 防止重入：modemInit() 内部会调 server.handleClient()，
  // 若浏览器超时重试会导致嵌套调用，最终拖垮 WiFi
  if (!acquireModemPort("/modem")) return;

  String action = server.arg("action");
  String json = "{";
  bool success = false;
  String message = "";

  if (action == "restart" || action == "hardreset") {
    // 重启类操作入队后立即响应，loop 中由任务队列执行（浏览器不再挂着等）
    ModemJobType jt = (action == "restart") ? MODEM_JOB_SOFT_RESET : MODEM_JOB_HARD_RESET;
    releaseModemPort();  // 释放同步占用的锁，改由任务队列在执行时持有
    uint32_t id = jobsSubmit(jt);
    if (id == 0) {
      server.send(200, "application/json", "{\"success\":false,\"message\":\"任务队列忙，请稍后重试\"}");
      return;
    }
    logCaptureLn(String(("网页端请求" + String(action == "restart" ? "软" : "硬") + "重启模组（任务" + String(id) + "）...")));
    server.send(200, "application/json",
                ("{\"queued\":true,\"id\":" + String(id) +
                 ",\"message\":\"正在" + String(action == "restart" ? "软" : "硬") +
                 "重启模组，请等待约 15 秒后刷新页面\"}").c_str());
    return;
  }
  else if (action == "signal") {
    logCaptureLn(String("网页端查询信号: AT+CSQ"));
    String resp = sendATCommand("AT+CSQ", 3000);
    int ber = -1;
    int csqIdx = resp.indexOf("+CSQ:");
    int commaIdx = resp.indexOf(',', csqIdx);
    if (commaIdx >= 0) ber = resp.substring(commaIdx + 1).toInt();
    int dbm;
    if (modemParseCsq(resp, dbm)) {
      String quality;
      if (dbm >= -75) quality = "优秀";
      else if (dbm >= -85) quality = "良好";
      else if (dbm >= -95) quality = "一般";
      else if (dbm >= -105) quality = "较差";
      else quality = "很差";
      message = "RSSI: " + String(dbm) + " dBm (" + quality + ")" +
                (ber >= 0 ? ", BER: " + String(ber) : "");
      success = true;
      modemSignalCache = String(dbm) + " dBm";  // 手动查询成功顺带刷新概览缓存
    }
    if (!success) message = "无法获取信号: " + resp;
  }
  else if (action == "operator") {
    logCaptureLn(String("网页端查询运营商: AT+COPS?"));
    String resp = sendATCommand("AT+COPS?", 5000);
    String op = modemParseCops(resp);
    if (op.length() > 0) {
      message = op;
      success = true;
      modemOperatorCache = op;  // 顺带刷新概览缓存
    } else if (resp.indexOf("+COPS:") >= 0) {
      message = resp.substring(resp.indexOf("+COPS:"), resp.indexOf('\n') > 0 ? resp.indexOf('\n') : (unsigned int)resp.length());
      success = true;
    }
    if (!success) message = "无法获取运营商: " + resp;
  }
  else if (action == "imei") {
    logCaptureLn(String("网页端查询IMEI: AT+GSN"));
    String imei = modemQueryImei();
    if (imei.length() > 0) {
      message = imei;
      success = true;
      modemImeiCache = imei;  // 顺带刷新概览缓存
    } else {
      message = "无法获取 IMEI";
    }
  }
  else {
    message = "未知操作: " + action;
  }

  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  releaseModemPort();
  server.send(200, "application/json", json);
}

// WiFi 重启
void handleWifi() {
  if (!checkAuth()) return;

  static bool busy = false;
  if (busy) {
    server.send(429, "application/json", "{\"success\":false,\"message\":\"WiFi正忙，请稍后重试\"}");
    return;
  }
  busy = true;

  String action = server.arg("action");
  if (action == "restart") {
    logCaptureLn(String("网页端请求重启WiFi..."));
    server.send(200, "application/json", "{\"success\":true,\"message\":\"WiFi 正在重启，请等待约 5 秒后刷新页面\"}");
    WiFi.disconnect(true);
    delay(500);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    WiFi.setAutoReconnect(true);
    WiFi.setScanMethod(WIFI_FAST_SCAN);
    const char* rs; const char* rp;
    getPrimaryWifi(rs, rp);
    WiFi.begin(rs, rp);
    logCaptureLn(String("正在重新连接WiFi: " + String(rs)));
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      wdtFeed();
      delay(50);
      server.handleClient();
    }
    if (WiFi.status() == WL_CONNECTED) {
      logCaptureLn(String("WiFi 重连成功, IP: " + WiFi.localIP().toString()));
    } else {
      logCaptureLn(String("WiFi 重连失败，将在后台持续尝试"));
    }
  } else {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"未知操作\"}");
  }
  busy = false;
}

// 系统控制命令
void handleSystem() {
  if (!checkAuth()) return;

  String action = server.arg("action");
  if (action == "restart") {
    // 整机重启 — 先响应浏览器再重启，防止浏览器超时重试
    logCaptureLn(String("网页端请求重启系统..."));
    server.send(200, "application/json", "{\"success\":true,\"message\":\"系统正在重启，请等待约 30 秒后刷新页面\"}");
    delay(500);  // 确保响应完整发出
    logCaptureLn(String("系统重启中..."));
    ESP.restart();
  } else {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"未知操作\"}");
  }
}

// 概览页轻量状态（自动刷新用，只读缓存，不碰模组串口）
void handleStatus() {
  if (!checkAuth()) return;

  bool emailOk = config.smtpServer.length() > 0 && config.smtpUser.length() > 0 &&
                 config.smtpPass.length() > 0 && config.smtpSendTo.length() > 0;
  int pushCount = 0;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (config.pushChannels[i].enabled) pushCount++;
  }
  long up = millis() / 1000;
  char upBuf[16];
  snprintf(upBuf, sizeof(upBuf), "%ld:%02ld:%02ld", up / 3600, (up % 3600) / 60, up % 60);

  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"ssid\":\"" + jsonEscape(String(WiFi.SSID())) + "\",";
  json += "\"heap\":" + String(ESP.getFreeHeap() / 1024) + ",";
  json += "\"uptime\":\"" + String(upBuf) + "\",";
  json += "\"modem\":" + String(modemReady ? "true" : "false") + ",";
  json += "\"signal\":\"" + jsonEscape(modemSignalCache) + "\",";
  json += "\"operator\":\"" + jsonEscape(modemOperatorCache) + "\",";
  json += "\"imei\":\"" + jsonEscape(modemImeiCache) + "\",";
  json += "\"iccid\":\"" + jsonEscape(modemIccidCache) + "\",";
  json += "\"model\":\"" + jsonEscape(modemModelCache) + "\",";
  json += "\"fw\":\"" + jsonEscape(modemFwCache) + "\",";
  json += "\"email\":" + String(emailOk ? "true" : "false") + ",";
  json += "\"push\":" + String(pushCount) + ",";
  json += "\"channels\":" + pushChannelStatsJson();
  json += "}";
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "application/json", json);
}

// 短信记录查询（?clear=1 清空）
void handleSmsLog() {
  if (!checkAuth()) return;

  if (server.arg("clear") == "1") {
    recordsClear();
    logCaptureLn(String("网页端清空了短信记录"));
    server.send(200, "application/json", "{\"total\":0,\"items\":[]}");
    return;
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "application/json", recordsJson());
}

// 推送通道测试（走 WiFi，不占用模组串口）
void handleTestPush() {
  if (!checkAuth()) return;

  int ch = server.arg("ch").toInt();
  bool ok = false;
  String message;

  if (ch < 0 || ch >= MAX_PUSH_CHANNELS) {
    message = "无效的通道编号";
  } else if (!isPushChannelValid(config.pushChannels[ch])) {
    message = "通道未启用或配置不完整，请先填写并保存";
  } else {
    // 测试时间戳
    String ts = "";
    time_t now = time(nullptr);
    if (now > 100000) {
      struct tm ti;
      gmtime_r(&now, &ti);
      char b[24];
      strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S UTC", &ti);
      ts = b;
    }
    ok = sendToChannel(config.pushChannels[ch], "测试",
                       "这是一条来自 Web 管理页的测试推送", ts.c_str(), ch);
    message = ok ? "测试推送已发出，请到对应平台查收" : "测试推送失败，请到系统日志查看原因";
  }

  String json = "{";
  json += "\"success\":" + String(ok ? "true" : "false") + ",";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// CSV 字段转义：含逗号/引号/换行的字段包引号，内部引号翻倍
static String csvField(const String& s) {
  String safe = s;
  // 短信内容属于不可信输入；Excel/表格软件会把 = + - @ 开头的字段当公式执行。
  // 前置单引号既阻止公式注入，也能保留 +86 手机号的文本格式。
  if (safe.length() > 0 && (safe[0] == '=' || safe[0] == '+' || safe[0] == '-' || safe[0] == '@')) {
    safe = "'" + safe;
  }
  bool needQuote = safe.indexOf(',') >= 0 || safe.indexOf('"') >= 0 || safe.indexOf('\n') >= 0;
  if (!needQuote) return safe;
  String r = "\"";
  for (unsigned int i = 0; i < safe.length(); i++) {
    if (safe.charAt(i) == '"') r += "\"\"";
    else r += safe.charAt(i);
  }
  r += "\"";
  return r;
}

// 短信记录导出 CSV（全量历史，含 UTF-8 BOM，Excel 打开中文不乱码）
void handleRecordsExport() {
  if (!checkAuth()) return;
  logCaptureLn(String("网页端导出短信记录 CSV"));
  server.sendHeader("Content-Disposition", "attachment; filename=sms_records.csv");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent("\xEF\xBB\xBF");  // UTF-8 BOM
  server.sendContent("sender,timestamp,text,email_ok,push_mask,push_enabled\n");

  for (int pass = 0; pass < 2; pass++) {
    const char* path = (pass == 0) ? RECORDS_OLD : RECORDS_FILE;
    if (!LittleFS.exists(path)) continue;
    File f = LittleFS.open(path, "r");
    if (!f) continue;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      // TSV → CSV：按 tab 切 6 字段
      String fields[6];
      int fi = 0, start = 0;
      while (fi < 5) {
        int tab = line.indexOf('\t', start);
        if (tab < 0) break;
        fields[fi++] = line.substring(start, tab);
        start = tab + 1;
      }
      fields[fi] = line.substring(start);
      if (fi < 5) continue;
      String csv = csvField(fields[0]) + "," + csvField(fields[2]) + "," + csvField(fields[1]) + "," +
                   fields[3] + "," + fields[4] + "," + fields[5] + "\n";
      server.sendContent(csv);
    }
    f.close();
  }
  server.sendContent("");  // 结束 chunked 响应
}

// 配置导出 JSON（?plain=1 含密钥，默认打码）
void handleConfigExport() {
  if (!checkAuth()) return;
  bool plain = (server.hasArg("plain") && server.arg("plain") == "1");
  logCaptureLn(String(plain ? "导出配置（含密钥）" : "导出配置（密钥打码）"));
  server.sendHeader("Content-Disposition", "attachment; filename=sms_config.json");
  server.send(200, "application/json", configToJson(!plain));
}

// 配置导入 JSON（POST body）
void handleConfigImport() {
  if (!checkAuth()) return;
  String body = server.arg("plain");  // WebServer 把 POST 原始 body 放在 "plain"
  if (body.length() == 0) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"请求体为空\"}");
    return;
  }
  logCaptureLn(String("网页端导入配置（") + String(body.length()) + " 字节）");
  String err = configFromJson(body);
  if (err.length() > 0) {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"" + jsonEscape(err) + "\"}");
    return;
  }
  configValid = isConfigValid();
  server.send(200, "application/json", "{\"success\":true,\"message\":\"配置已恢复并保存，页面即将刷新\"}");
}
