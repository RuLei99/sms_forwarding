#include "push.h"
#include "web_handlers.h"
#include "config.h"
#include <HTTPClient.h>
#include <mbedtls/md.h>
#include <base64.h>
#include <sys/time.h>
#include <esp_task_wdt.h>
#include <WiFiClient.h>
#include <PubSubClient.h>

// 发送邮件通知函数（带重试）。返回是否最终成功
bool sendEmailNotification(const char* subject, const char* body) {
  if (config.smtpServer.length() == 0 || config.smtpUser.length() == 0 ||
      config.smtpPass.length() == 0 || config.smtpSendTo.length() == 0) {
    logCaptureLn(String("邮件配置不完整，跳过发送"));
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    logCaptureLn(String("WiFi未连接，跳过邮件发送"));
    return false;
  }

  auto statusCallback = [](SMTPStatus status) {
    logCaptureLn(String(status.text));
  };

  const int MAX_ATTEMPTS = 3; // 总尝试次数（含首次）
  for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    esp_task_wdt_reset();  // 邮件整体可达数十秒，按尝试粒度喂狗
    if (attempt > 1) {
      delay(1000 * (attempt - 1)); // 退避：第2次前等1秒，第3次前等2秒
      logCaptureF("[邮件] 重试 (%d/%d)...\n", attempt, MAX_ATTEMPTS);
    }

    // 清理上一次尝试的残留连接状态，确保每次从干净状态开始
    smtp.stop();

    if (!smtp.connect(config.smtpServer.c_str(), config.smtpPort, statusCallback)) {
      logCaptureLn(String("邮件服务器连接失败"));
      continue;
    }

    if (!smtp.authenticate(config.smtpUser.c_str(), config.smtpPass.c_str(), readymail_auth_password)) {
      // 认证失败属配置错误，重试无意义
      logCaptureLn(String("邮件认证失败（请检查账号与SMTP授权码），停止重试"));
      smtp.stop();
      return false;
    }

    SMTPMessage msg;
    String from = "sms notify <"; from += config.smtpUser; from += ">";
    msg.headers.add(rfc822_from, from.c_str());
    String to = "your_email <"; to += config.smtpSendTo; to += ">";
    msg.headers.add(rfc822_to, to.c_str());
    msg.headers.add(rfc822_subject, subject);
    msg.text.body(body);
    msg.timestamp = time(nullptr);

    if (smtp.send(msg)) {
      logCaptureF("[邮件] 发送成功（第 %d/%d 次尝试）\n", attempt, MAX_ATTEMPTS);
      smtp.stop();
      return true;
    }
    logCaptureLn(String("邮件发送失败"));
  }

  logCaptureLn(String("邮件多次重试后仍失败，本次通知已丢弃"));
  smtp.stop();
  return false;
}

// URL编码辅助函数
String urlEncode(const String& str) {
  String encoded = "";
  encoded.reserve(str.length() * 3 / 2 + 8);  // 减少逐字符 += 的堆重新分配
  unsigned char c;
  char code0;
  char code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = (unsigned char)str.charAt(i);  // 必须 unsigned：UTF-8 中文字节 >0x7F，负值传入 isalnum 是未定义行为
    if (c == ' ') {
      encoded += '+';
    } else if (isalnum(c)) {
      encoded += (char)c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

// 钉钉签名函数（时间戳为UTC毫秒级）
String dingtalkSign(const String& secret, int64_t timestamp) {
  String stringToSign = String(timestamp) + "\n" + secret;

  uint8_t hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)secret.c_str(), secret.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);

  String base64Encoded = base64::encode(hmacResult, 32);
  return urlEncode(base64Encoded);
}

// 获取当前UTC毫秒级时间戳（用于钉钉签名）
int64_t getUtcMillis() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) == 0) {
    return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
  }
  // 如果获取失败，使用time()函数
  return (int64_t)time(nullptr) * 1000LL;
}

// JSON转义函数
String jsonEscape(const String& str) {
  String result = "";
  result.reserve(str.length() + 16);
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == '"') result += "\\\"";
    else if (c == '\\') result += "\\\\";
    else if (c == '\n') result += "\\n";
    else if (c == '\r') result += "\\r";
    else if (c == '\t') result += "\\t";
    else if ((unsigned char)c < 0x20) {
      // 其余控制字符（短信中可能出现）必须转成 \u00XX，否则生成非法 JSON，前端解析失败
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
      result += buf;
    }
    else result += c;
  }
  return result;
}

// ---- 通道健康统计与熔断 ----
struct ChannelStat {
  uint32_t ok;
  uint32_t fail;
  uint32_t blocked;        // 因熔断被跳过的短信数
  uint8_t consecFail;      // 连续失败次数
  uint32_t cooldownUntil;  // 熔断截止时刻（millis）
};
static ChannelStat s_stats[MAX_PUSH_CHANNELS];

// 连续失败 5 次进入熔断；冷却 30 分钟起步、连续失败越多翻倍（封顶 4 小时）；
// 冷却结束后下一条短信即半开探测，成功则完全恢复
static void noteChannelResult(int idx, bool ok) {
  if (idx < 0 || idx >= MAX_PUSH_CHANNELS) return;
  ChannelStat& s = s_stats[idx];
  if (ok) {
    s.ok++;
    if (s.consecFail >= 5) logCaptureLn(String("[通道") + String(idx + 1) + "] 恢复正常");
    s.consecFail = 0;
    s.cooldownUntil = 0;
    return;
  }
  s.fail++;
  s.consecFail++;
  if (s.consecFail == 5) {
    s.cooldownUntil = millis() + 30UL * 60UL * 1000UL;
    logCaptureLn(String("⚠️ [通道") + String(idx + 1) + "] 连续失败 5 次，熔断 30 分钟");
  } else if (s.consecFail > 5) {
    uint32_t shifts = s.consecFail - 5;
    if (shifts > 3) shifts = 3;  // 30min→1h→2h→4h 封顶
    s.cooldownUntil = millis() + ((30UL * 60UL * 1000UL) << shifts);
  }
}

bool pushChannelCooling(int idx) {
  if (idx < 0 || idx >= MAX_PUSH_CHANNELS) return false;
  return s_stats[idx].cooldownUntil != 0 && millis() < s_stats[idx].cooldownUntil;
}

void pushStatsNoteBlocked(int idx) {
  if (idx < 0 || idx >= MAX_PUSH_CHANNELS) return;
  s_stats[idx].blocked++;
}

String pushChannelStatsJson() {
  String json = "[";
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (i > 0) json += ",";
    json += "{\"i\":" + String(i);
    json += ",\"ok\":" + String(s_stats[i].ok);
    json += ",\"fail\":" + String(s_stats[i].fail);
    json += ",\"blocked\":" + String(s_stats[i].blocked);
    json += ",\"cool\":" + String(pushChannelCooling(i) ? "true" : "false");
    json += "}";
  }
  json += "]";
  return json;
}

// 把文本通过所有有效通道推送 + 发邮件（每日健康报告用）
void pushBroadcastText(const char* title, const char* text) {
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) {
      sendToChannel(config.pushChannels[i], "健康报告", text, "", i);
    }
  }
  sendEmailNotification(title, text);
}

// 判断业务响应体是否表示成功
// 部分平台无论成败 HTTP 都返回 200，错误信息在响应体中，需要额外校验
static bool isBodySuccess(const PushChannel& channel, const String& resp) {
  switch (channel.type) {
    case PUSH_TYPE_DINGTALK:   return resp.indexOf("\"errcode\":0") >= 0;
    case PUSH_TYPE_FEISHU:     return resp.indexOf("\"code\":0") >= 0;
    case PUSH_TYPE_PUSHPLUS:   return resp.indexOf("\"code\":200") >= 0;
    case PUSH_TYPE_SERVERCHAN: return resp.indexOf("\"code\":0") >= 0;
    case PUSH_TYPE_TELEGRAM:   return resp.indexOf("\"ok\":true") >= 0;
    default:                   return true; // 其余平台以 HTTP 状态码为准
  }
}

// 带重试地执行单个通道的 HTTP 请求（最多 3 次，失败后退避重试）
static bool executeChannelRequest(const PushChannel& channel, const String& url,
                                  bool isGet, const String& contentType, const String& body,
                                  const String& channelName) {
  const int MAX_ATTEMPTS = 3;
  for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    esp_task_wdt_reset();  // 单通道最多 3 次重试，按尝试粒度喂狗
    if (attempt > 1) {
      delay(500 * (attempt - 1)); // 退避：第2次前等0.5秒，第3次前等1秒
      logCaptureF("[%s] 重试 (%d/%d)...\n", channelName.c_str(), attempt, MAX_ATTEMPTS);
    }

    HTTPClient http;
    http.begin(url);
    if (!isGet) http.addHeader("Content-Type", contentType);
    http.setTimeout(10000);
    int httpCode = isGet ? http.GET() : http.POST(body);

    if (httpCode <= 0) {
      // 连接失败/超时等传输层错误，可重试
      logCaptureF("[%s] HTTP请求失败: %s\n", channelName.c_str(), http.errorToString(httpCode).c_str());
    } else {
      logCaptureF("[%s] 响应码: %d\n", channelName.c_str(), httpCode);
      bool httpOk = (httpCode >= 200 && httpCode < 300);
      String resp = "";
      if (httpOk) resp = http.getString();

      if (httpOk && isBodySuccess(channel, resp)) {
        logCaptureF("[%s] 推送成功（第 %d/%d 次尝试）\n", channelName.c_str(), attempt, MAX_ATTEMPTS);
        if (resp.length() > 0) logCaptureLn(String("响应: " + resp));
        http.end();
        return true;
      }

      if (resp.length() > 0) logCaptureLn(String("响应: " + resp));

      // 明确的 4xx 客户端错误（除 429 限流）多为配置问题，重试无意义
      if (httpCode >= 400 && httpCode < 500 && httpCode != 429) {
        logCaptureF("[%s] 客户端错误 %d，跳过重试\n", channelName.c_str(), httpCode);
        http.end();
        return false;
      }
      logCaptureLn(String("[" + channelName + "] 本次推送未确认成功"));
    }
    http.end();
  }

  logCaptureLn(String("[" + channelName + "] 多次重试后仍失败"));
  return false;
}

// ---- 通道描述符表（插件化）----
// 新增推送通道 = 枚举加类型 + 此表加一行 build 函数，校验/Web 选项/名称全部自动跟随
struct PushChannelArgs {
  const char* sender;
  const char* message;
  const char* timestamp;
  String senderEsc;   // JSON 转义后
  String msgEsc;
  String tsEsc;
};

struct PushChannelDesc {
  PushType type;
  const char* name;
  bool needUrl;       // URL 是否必填（有 defUrl 时可留空）
  bool needKey1;
  bool needKey2;
  const char* defUrl; // URL 留空时的默认值（空串 = 无默认、必须填写）
  void (*build)(const PushChannel&, const PushChannelArgs&, String& url, String& body, String& contentType, bool& isGet);
};

static void buildPostJson(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  url = ch.url;
  body = "{\"sender\":\"" + a.senderEsc + "\",\"message\":\"" + a.msgEsc + "\",\"timestamp\":\"" + a.tsEsc + "\"}";
  (void)ct; (void)isGet;
}

static void buildBark(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  url = ch.url;
  body = "{\"title\":\"" + a.senderEsc + "\",\"body\":\"" + a.msgEsc + "\"}";
  (void)ct; (void)isGet;
}

static void buildGet(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  String u = ch.url;
  u += (u.indexOf('?') == -1) ? "?" : "&";
  u += "sender=" + urlEncode(String(a.sender));
  u += "&message=" + urlEncode(String(a.message));
  u += "&timestamp=" + urlEncode(String(a.timestamp));
  url = u;
  isGet = true;
  (void)body; (void)ct;
}

static void buildDingtalk(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  String webhookUrl = ch.url;
  if (ch.key1.length() > 0) {
    // 加签：UTC 毫秒时间戳 + HMAC-SHA256
    int64_t ts = getUtcMillis();
    String sign = dingtalkSign(ch.key1, ts);
    webhookUrl += (webhookUrl.indexOf('?') == -1) ? "?" : "&";
    char tsBuf[21];
    snprintf(tsBuf, sizeof(tsBuf), "%lld", ts);
    webhookUrl += "timestamp=" + String(tsBuf) + "&sign=" + sign;
  }
  url = webhookUrl;
  body = "{\"msgtype\":\"text\",\"text\":{\"content\":\"📱短信通知\\n发送者: " + a.senderEsc +
         "\\n内容: " + a.msgEsc + "\\n时间: " + a.tsEsc + "\"}}";
  (void)ct; (void)isGet;
}

static void buildPushPlus(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  url = ch.url.length() > 0 ? ch.url : "http://www.pushplus.plus/send";
  String channelValue = "wechat";
  if (ch.key2.length() > 0) {
    // 仅支持微信公众号（wechat）、浏览器插件（extension）和 PushPlus App（app）三种渠道
    if (ch.key2 == "wechat" || ch.key2 == "extension" || ch.key2 == "app") {
      channelValue = ch.key2;
    } else {
      logCaptureLn(String("Invalid PushPlus channel '" + ch.key2 + "'. Using default 'wechat'."));
    }
  }
  body = "{\"token\":\"" + jsonEscape(ch.key1) + "\",\"title\":\"短信来自: " + a.senderEsc +
         "\",\"content\":\"<b>发送者:</b> " + a.senderEsc + "<br><b>时间:</b> " + a.tsEsc +
         "<br><b>内容:</b><br>" + a.msgEsc + "\",\"channel\":\"" + channelValue + "\"}";
  (void)ct; (void)isGet;
}

static void buildServerChan(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  url = ch.url.length() > 0 ? ch.url : ("https://sctapi.ftqq.com/" + ch.key1 + ".send");
  ct = "application/x-www-form-urlencoded";
  body = "title=" + urlEncode("短信来自: " + String(a.sender));
  body += "&desp=" + urlEncode("**发送者:** " + String(a.sender) + "\n\n**时间:** " + String(a.timestamp) +
                               "\n\n**内容:**\n\n" + String(a.message));
  (void)isGet;
}

static void buildCustom(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  url = ch.url;
  body = ch.customBody;
  body.replace("{sender}", a.senderEsc);
  body.replace("{message}", a.msgEsc);
  body.replace("{timestamp}", a.tsEsc);
  (void)ct; (void)isGet;
}

static void buildFeishu(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  String jsonData = "{";
  if (ch.key1.length() > 0) {
    // 飞书签名: base64(HMAC-SHA256(key=timestamp + "\n" + secret, msg=""))，秒级时间戳
    int64_t ts = time(nullptr);
    String stringToSign = String(ts) + "\n" + ch.key1;
    uint8_t hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);
    String sign = base64::encode(hmacResult, 32);
    jsonData += "\"timestamp\":\"" + String(ts) + "\",\"sign\":\"" + sign + "\",";
  }
  jsonData += "\"msg_type\":\"text\",\"content\":{\"text\":\"📱短信通知\\n发送者: " + a.senderEsc +
              "\\n内容: " + a.msgEsc + "\\n时间: " + a.tsEsc + "\"}}";
  url = ch.url;
  body = jsonData;
  (void)ct; (void)isGet;
}

static void buildGotify(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  String u = ch.url;
  if (!u.endsWith("/")) u += "/";
  u += "message?token=" + ch.key1;
  url = u;
  body = "{\"title\":\"短信来自: " + a.senderEsc + "\",\"message\":\"" + a.msgEsc +
         "\\n\\n时间: " + a.tsEsc + "\",\"priority\":5}";
  (void)ct; (void)isGet;
}

static void buildTelegram(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  String base = ch.url.length() > 0 ? ch.url : "https://api.telegram.org";
  if (base.endsWith("/")) base.remove(base.length() - 1);
  url = base + "/bot" + ch.key2 + "/sendMessage";
  String text = "📱短信通知\n发送者: " + a.senderEsc + "\n内容: " + a.msgEsc + "\n时间: " + a.tsEsc;
  body = "{\"chat_id\":\"" + jsonEscape(ch.key1) + "\",\"text\":\"" + text + "\"}";
  (void)ct; (void)isGet;
}

static void buildNtfy(const PushChannel& ch, const PushChannelArgs& a, String& url, String& body, String& ct, bool& isGet) {
  // ntfy JSON 发布：POST 到服务器根地址，topic 放请求体（url 留空用官方 ntfy.sh）
  url = ch.url.length() > 0 ? ch.url : "https://ntfy.sh";
  body = "{\"topic\":\"" + jsonEscape(ch.key1) + "\",\"title\":\"短信来自: " + a.senderEsc +
         "\",\"message\":\"" + a.msgEsc + "\",\"tags\":[\"iphone\"]}";
  (void)ct; (void)isGet;
}

static const PushChannelDesc PUSH_DESCS[] = {
  {PUSH_TYPE_POST_JSON,  "POST JSON（通用格式）",  true,  false, false, "",        buildPostJson},
  {PUSH_TYPE_BARK,       "Bark（iOS推送）",        true,  false, false, "",        buildBark},
  {PUSH_TYPE_GET,        "GET请求（参数在URL中）", true,  false, false, "",        buildGet},
  {PUSH_TYPE_DINGTALK,   "钉钉机器人",             true,  false, false, "",        buildDingtalk},
  {PUSH_TYPE_PUSHPLUS,   "PushPlus",               false, true,  false, "http://www.pushplus.plus/send", buildPushPlus},
  {PUSH_TYPE_SERVERCHAN, "Server酱",               false, true,  false, "",        buildServerChan},
  {PUSH_TYPE_CUSTOM,     "自定义模板",             true,  false, false, "",        buildCustom},
  {PUSH_TYPE_FEISHU,     "飞书机器人",             true,  false, false, "https://open.feishu.cn/open-apis/bot/v2/hook/", buildFeishu},
  {PUSH_TYPE_GOTIFY,     "Gotify",                 true,  true,  false, "",        buildGotify},
  {PUSH_TYPE_TELEGRAM,   "Telegram Bot",           false, true,  true,  "https://api.telegram.org", buildTelegram},
  {PUSH_TYPE_NTFY,       "ntfy",                   false, true,  false, "https://ntfy.sh", buildNtfy},
  // MQTT 走专用执行路径（非 HTTP），build 置空
  {PUSH_TYPE_MQTT,       "MQTT",                   true,  true,  false, "",        nullptr},
};

size_t pushTypeCount() { return sizeof(PUSH_DESCS) / sizeof(PUSH_DESCS[0]); }
const char* pushTypeName(PushType t) {
  for (const auto& d : PUSH_DESCS) if (d.type == t) return d.name;
  return "";
}

static const PushChannelDesc* findDesc(PushType t) {
  for (const auto& d : PUSH_DESCS) if (d.type == t) return &d;
  return nullptr;
}

// 按描述符表校验：needXxx 的字段必填；URL 有默认值（defUrl 非空）时允许留空
bool pushChannelFieldsValid(const PushChannel& ch) {
  const PushChannelDesc* d = findDesc(ch.type);
  if (!d) return false;
  if (d->needUrl && ch.url.length() == 0 && String(d->defUrl).length() == 0) return false;
  if (d->needKey1 && ch.key1.length() == 0) return false;
  if (d->needKey2 && ch.key2.length() == 0) return false;
  return true;
}

// ---- MQTT 执行路径（PubSubClient，同步发布） ----
static bool executeMqttPublish(const PushChannel& ch, const PushChannelArgs& a, const String& channelName) {
  // url 形如 host:port 或 mqtt://host:port（默认端口 1883）；key2 可选 user:pass
  String host = ch.url;
  if (host.startsWith("mqtt://")) host = host.substring(7);
  else if (host.startsWith("mqtts://")) {
    logCaptureLn(String("[" + channelName + "] mqtts(TLS) 暂不支持，请用 1883 明文端口"));
    return false;
  }
  int port = 1883;
  int cIdx = host.lastIndexOf(':');
  if (cIdx > 0 && host.indexOf('/', cIdx) < 0) {
    port = host.substring(cIdx + 1).toInt();
    host = host.substring(0, cIdx);
  }
  if (host.length() == 0 || port <= 0 || port > 65535) {
    logCaptureLn(String("[" + channelName + "] MQTT 地址格式错误（应为 host:port）"));
    return false;
  }

  String user = "", pass = "";
  if (ch.key2.length() > 0) {
    int colon = ch.key2.indexOf(':');
    if (colon > 0) {
      user = ch.key2.substring(0, colon);
      pass = ch.key2.substring(colon + 1);
    }
  }

  String payload = "📱短信通知\n发送者: " + String(a.sender) + "\n内容: " + String(a.message) + "\n时间: " + String(a.timestamp);

  WiFiClient netClient;
  PubSubClient mqtt(netClient);
  mqtt.setServer(host.c_str(), port);
  mqtt.setBufferSize(1024);  // payload 可能超过默认 256

  const int MAX_ATTEMPTS = 2;
  for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    esp_task_wdt_reset();
    if (attempt > 1) {
      delay(500);
      logCaptureF("[%s] MQTT 重试 (%d/%d)...\n", channelName.c_str(), attempt, MAX_ATTEMPTS);
    }
    if (!mqtt.connect("sms_forwarder", user.length() ? user.c_str() : nullptr,
                      pass.length() ? pass.c_str() : nullptr)) {
      logCaptureLn(String("[" + channelName + "] MQTT 连接失败（state=" + String(mqtt.state()) + "）"));
      continue;
    }
    bool ok = mqtt.publish(ch.key1.c_str(), payload.c_str());
    mqtt.loop();
    mqtt.disconnect();
    logCaptureF("[%s] MQTT %s（topic=%s）\n", channelName.c_str(), ok ? "发布成功" : "发布失败", ch.key1.c_str());
    return ok;
  }
  return false;
}

// 发送单个推送通道（查描述符表构造请求；MQTT 走专用路径）
bool sendToChannel(const PushChannel& channel, const char* sender, const char* message, const char* timestamp, int idx) {
  if (!channel.enabled) return false;

  const PushChannelDesc* desc = findDesc(channel.type);
  if (!desc) {
    logCaptureLn(String("未知推送类型"));
    return false;
  }

  String channelName = channel.name.length() > 0 ? channel.name : ("通道" + String(channel.type));
  logCaptureLn(String("发送到推送通道: " + channelName));

  PushChannelArgs args;
  args.sender = sender;
  args.message = message;
  args.timestamp = timestamp;
  args.senderEsc = jsonEscape(String(sender));
  args.msgEsc = jsonEscape(String(message));
  args.tsEsc = jsonEscape(String(timestamp));

  bool ok = false;
  if (channel.type == PUSH_TYPE_MQTT) {
    ok = executeMqttPublish(channel, args, channelName);
  } else {
    String reqUrl = "", reqBody = "", reqContentType = "application/json";
    bool useGet = false;
    desc->build(channel, args, reqUrl, reqBody, reqContentType, useGet);
    logCaptureLn(String("[" + channelName + "] " + (useGet ? "GET " + reqUrl : reqBody)));
    ok = executeChannelRequest(channel, reqUrl, useGet, reqContentType, reqBody, channelName);
  }

  noteChannelResult(idx, ok);
  return ok;
}

// 发送短信到所有启用的推送通道，返回位图（bit i = 通道 i+1 成功）
uint8_t sendSMSToServer(const char* sender, const char* message, const char* timestamp) {
  if (WiFi.status() != WL_CONNECTED) {
    logCaptureLn(String("WiFi未连接，跳过推送"));
    return 0;
  }

  bool hasEnabledChannel = false;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) {
      hasEnabledChannel = true;
      break;
    }
  }

  if (!hasEnabledChannel) {
    logCaptureLn(String("没有启用的推送通道"));
    return 0;
  }

  uint8_t mask = 0;
  logCaptureLn(String("\n=== 开始多通道推送 ==="));
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) {
      if (pushChannelCooling(i)) {
        // 熔断中：跳过发送（冷却结束后的下一条即半开探测）
        pushStatsNoteBlocked(i);
        logCaptureLn(String("⏸️ [通道" + String(i + 1) + "] 熔断冷却中，本条跳过"));
        continue;
      }
      if (sendToChannel(config.pushChannels[i], sender, message, timestamp, i)) {
        mask |= (1 << i);
      }
      delay(100); // 短暂延迟避免请求过快
    }
  }
  logCaptureLn(String("=== 多通道推送完成 ===\n"));
  return mask;
}
