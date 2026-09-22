#include "config.h"
#include "web_handlers.h"
#include "push.h"
#include "wifi_config.h"

// 保存配置到NVS
void saveConfig() {
  if (!preferences.begin("sms_config", false)) {
    logCaptureLn(String("⚠️ NVS 打开失败，配置未保存（分区损坏或空间不足）"));
    return;
  }
  int failCnt = 0;
  auto putS = [&](const char* k, const String& v) { if (preferences.putString(k, v) == 0 && v.length() > 0) failCnt++; };
  putS("smtpServer", config.smtpServer);
  if (!preferences.putInt("smtpPort", config.smtpPort)) failCnt++;
  putS("smtpUser", config.smtpUser);
  putS("smtpPass", config.smtpPass);
  putS("smtpSendTo", config.smtpSendTo);
  putS("adminPhone", config.adminPhone);
  putS("webUser", config.webUser);
  putS("webPass", config.webPass);
  putS("numBlkList", config.numberBlackList);
  if (!preferences.putBool("smsOnly", config.smsOnly)) failCnt++;
  if (!preferences.putBool("fltWL", config.filterWhitelist)) failCnt++;
  putS("fltKw", config.filterKeywords);
  if (!preferences.putInt("tz", config.tzHours)) failCnt++;
  if (!preferences.putBool("report", config.reportEnabled)) failCnt++;
  putS("wifi1ssid", config.wifi1Ssid);
  putS("wifi1pass", config.wifi1Pass);
  putS("wifi2ssid", config.wifi2Ssid);
  putS("wifi2pass", config.wifi2Pass);

  // 保存推送通道配置
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    String prefix = "push" + String(i);
    if (!preferences.putBool((prefix + "en").c_str(), config.pushChannels[i].enabled)) failCnt++;
    if (!preferences.putUChar((prefix + "type").c_str(), (uint8_t)config.pushChannels[i].type)) failCnt++;
    putS((prefix + "url").c_str(), config.pushChannels[i].url);
    putS((prefix + "name").c_str(), config.pushChannels[i].name);
    putS((prefix + "k1").c_str(), config.pushChannels[i].key1);
    putS((prefix + "k2").c_str(), config.pushChannels[i].key2);
    putS((prefix + "body").c_str(), config.pushChannels[i].customBody);
  }

  preferences.end();
  if (failCnt > 0) {
    logCaptureLn(String("⚠️ 配置保存完成，但 ") + String(failCnt) + " 项 NVS 写入失败，重启后可能丢失");
  } else {
    logCaptureLn(String("配置已保存"));
  }
}

// 从NVS加载配置
void loadConfig() {
  preferences.begin("sms_config", true);
  config.smtpServer = preferences.getString("smtpServer", "");
  config.smtpPort = preferences.getInt("smtpPort", DEFAULT_SMTP_PORT);
  config.smtpUser = preferences.getString("smtpUser", "");
  config.smtpPass = preferences.getString("smtpPass", "");
  config.smtpSendTo = preferences.getString("smtpSendTo", "");
  config.adminPhone = preferences.getString("adminPhone", "");
  config.webUser = preferences.getString("webUser", DEFAULT_WEB_USER);
  config.webPass = preferences.getString("webPass", DEFAULT_WEB_PASS);
  config.numberBlackList = preferences.getString("numBlkList", "");
  // 默认开启：仅收短信不消耗流量，适合漫游卡；老用户升级后可在“模组控制”页解锁
  config.smsOnly = preferences.getBool("smsOnly", true);
  config.filterWhitelist = preferences.getBool("fltWL", false);
  config.filterKeywords = preferences.getString("fltKw", "");
  config.tzHours = preferences.getInt("tz", 8);
  if (config.tzHours < -12 || config.tzHours > 14) config.tzHours = 8;
  config.reportEnabled = preferences.getBool("report", true);
  config.wifi1Ssid = preferences.getString("wifi1ssid", "");
  config.wifi1Pass = preferences.getString("wifi1pass", "");
  config.wifi2Ssid = preferences.getString("wifi2ssid", "");
  config.wifi2Pass = preferences.getString("wifi2pass", "");

  // NVS 脏数据防御：端口/类型超出合法范围时回退默认值，避免后续连接/推送诡异失败
  if (config.smtpPort <= 0 || config.smtpPort > 65535) {
    logCaptureLn(String("⚠️ SMTP 端口配置异常(") + String(config.smtpPort) + ")，回退默认 " + String(DEFAULT_SMTP_PORT));
    config.smtpPort = DEFAULT_SMTP_PORT;
  }

  // 加载推送通道配置
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    String prefix = "push" + String(i);
    config.pushChannels[i].enabled = preferences.getBool((prefix + "en").c_str(), false);
    uint8_t rawType = preferences.getUChar((prefix + "type").c_str(), PUSH_TYPE_POST_JSON);
    if (rawType < PUSH_TYPE_MIN || rawType > PUSH_TYPE_MAX) {
      logCaptureLn(String("⚠️ 通道 ") + String(i + 1) + " 推送类型异常(" + String(rawType) + ")，回退 POST JSON");
      rawType = PUSH_TYPE_POST_JSON;
    }
    config.pushChannels[i].type = (PushType)rawType;
    config.pushChannels[i].url = preferences.getString((prefix + "url").c_str(), "");
    config.pushChannels[i].name = preferences.getString((prefix + "name").c_str(), "通道" + String(i + 1));
    config.pushChannels[i].key1 = preferences.getString((prefix + "k1").c_str(), "");
    config.pushChannels[i].key2 = preferences.getString((prefix + "k2").c_str(), "");
    config.pushChannels[i].customBody = preferences.getString((prefix + "body").c_str(), "");
  }
  
  // 兼容旧配置：如果有旧的httpUrl配置，迁移到第一个通道
  String oldHttpUrl = preferences.getString("httpUrl", "");
  if (oldHttpUrl.length() > 0 && !config.pushChannels[0].enabled) {
    config.pushChannels[0].enabled = true;
    config.pushChannels[0].url = oldHttpUrl;
    config.pushChannels[0].type = preferences.getUChar("barkMode", 0) != 0 ? PUSH_TYPE_BARK : PUSH_TYPE_POST_JSON;
    config.pushChannels[0].name = "迁移通道";
    logCaptureLn(String("已迁移旧HTTP配置到推送通道1"));
  }
  
  preferences.end();
  logCaptureLn(String("配置已加载"));
}

// 检查推送通道是否有效配置（字段需求查 push.cpp 描述符表，单一事实来源）
bool isPushChannelValid(const PushChannel& ch) {
  if (!ch.enabled) return false;
  return pushChannelFieldsValid(ch);
}

// 检查配置是否有效（至少配置了邮件或任一推送通道）
bool isConfigValid() {
  bool emailValid = config.smtpServer.length() > 0 && 
                    config.smtpUser.length() > 0 && 
                    config.smtpPass.length() > 0 && 
                    config.smtpSendTo.length() > 0;
  
  bool pushValid = false;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) {
      pushValid = true;
      break;
    }
  }
  
  return emailValid || pushValid;
}

#include "config.h"
#include "web_handlers.h"
#include "push.h"
#include "wifi_config.h"
#include <ArduinoJson.h>

#define MASK "******"
static String maskedIf(bool mask, const String& v) { return (mask && v.length() > 0) ? MASK : v; }

String configToJson(bool maskSecrets) {
  JsonDocument doc;
  doc["smtpServer"] = config.smtpServer;
  doc["smtpPort"] = config.smtpPort;
  doc["smtpUser"] = config.smtpUser;
  doc["smtpPass"] = maskedIf(maskSecrets, config.smtpPass);
  doc["smtpSendTo"] = config.smtpSendTo;
  doc["adminPhone"] = config.adminPhone;
  doc["webUser"] = config.webUser;
  doc["webPass"] = maskedIf(maskSecrets, config.webPass);
  doc["numberBlackList"] = config.numberBlackList;
  doc["smsOnly"] = config.smsOnly;
  doc["filterWhitelist"] = config.filterWhitelist;
  doc["filterKeywords"] = config.filterKeywords;
  doc["tzHours"] = config.tzHours;
  doc["reportEnabled"] = config.reportEnabled;
  doc["wifi1Ssid"] = config.wifi1Ssid;
  doc["wifi1Pass"] = maskedIf(maskSecrets, config.wifi1Pass);
  doc["wifi2Ssid"] = config.wifi2Ssid;
  doc["wifi2Pass"] = maskedIf(maskSecrets, config.wifi2Pass);
  JsonArray chans = doc["channels"].to<JsonArray>();
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    const PushChannel& c = config.pushChannels[i];
    JsonObject o = chans.add<JsonObject>();
    o["enabled"] = c.enabled;
    o["type"] = (int)c.type;
    o["name"] = c.name;
    o["url"] = c.url;
    o["key1"] = maskedIf(maskSecrets, c.key1);
    o["key2"] = maskedIf(maskSecrets, c.key2);
    o["customBody"] = c.customBody;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String configFromJson(const String& json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return String("JSON 解析失败: ") + err.c_str();

  // 全部校验通过后才落地（先写临时副本，避免半套配置）
  Config tmp = config;

  if (doc["smtpServer"].is<const char*>()) tmp.smtpServer = doc["smtpServer"].as<String>();
  if (doc["smtpPort"].is<int>()) {
    int port = doc["smtpPort"].as<int>();
    if (port <= 0 || port > 65535) return "smtpPort 超出范围";
    tmp.smtpPort = port;
  }
  if (doc["smtpUser"].is<const char*>()) tmp.smtpUser = doc["smtpUser"].as<String>();
  if (doc["smtpSendTo"].is<const char*>()) tmp.smtpSendTo = doc["smtpSendTo"].as<String>();
  if (doc["adminPhone"].is<const char*>()) tmp.adminPhone = doc["adminPhone"].as<String>();
  if (doc["webUser"].is<const char*>()) {
    String u = doc["webUser"].as<String>();
    if (u.length() > 0) tmp.webUser = u;
  }
  if (doc["numberBlackList"].is<const char*>()) tmp.numberBlackList = doc["numberBlackList"].as<String>();
  if (doc["smsOnly"].is<bool>()) tmp.smsOnly = doc["smsOnly"].as<bool>();
  if (doc["filterWhitelist"].is<bool>()) tmp.filterWhitelist = doc["filterWhitelist"].as<bool>();
  if (doc["filterKeywords"].is<const char*>()) tmp.filterKeywords = doc["filterKeywords"].as<String>();
  if (doc["tzHours"].is<int>()) {
    int tz = doc["tzHours"].as<int>();
    if (tz < -12 || tz > 14) return "tzHours 超出范围（-12~14）";
    tmp.tzHours = tz;
  }
  if (doc["reportEnabled"].is<bool>()) tmp.reportEnabled = doc["reportEnabled"].as<bool>();
  if (doc["wifi1Ssid"].is<const char*>()) tmp.wifi1Ssid = doc["wifi1Ssid"].as<String>();
  if (doc["wifi2Ssid"].is<const char*>()) tmp.wifi2Ssid = doc["wifi2Ssid"].as<String>();

  // 密钥类字段：打码值（MASK）跳过，保留当前值
  auto applySecret = [&](const char* key, String& dst) {
    if (doc[key].is<const char*>()) {
      String v = doc[key].as<String>();
      if (v != MASK && v.length() > 0) dst = v;
    }
  };
  applySecret("smtpPass", tmp.smtpPass);
  applySecret("webPass", tmp.webPass);
  applySecret("wifi1Pass", tmp.wifi1Pass);
  applySecret("wifi2Pass", tmp.wifi2Pass);

  if (doc["channels"].is<JsonArray>()) {
    JsonArray chans = doc["channels"].as<JsonArray>();
    int i = 0;
    for (JsonObject o : chans) {
      if (i >= MAX_PUSH_CHANNELS) break;
      if (o["enabled"].is<bool>()) tmp.pushChannels[i].enabled = o["enabled"].as<bool>();
      if (o["type"].is<int>()) {
        int ty = o["type"].as<int>();
        if (ty < PUSH_TYPE_MIN || ty > PUSH_TYPE_MAX) return String("通道 ") + String(i + 1) + " 类型非法";
        tmp.pushChannels[i].type = (PushType)ty;
      }
      if (o["name"].is<const char*>()) tmp.pushChannels[i].name = o["name"].as<String>();
      if (o["url"].is<const char*>()) tmp.pushChannels[i].url = o["url"].as<String>();
      if (o["customBody"].is<const char*>()) tmp.pushChannels[i].customBody = o["customBody"].as<String>();
      if (o["key1"].is<const char*>()) {
        String v = o["key1"].as<String>();
        if (v != MASK) tmp.pushChannels[i].key1 = v;
      }
      if (o["key2"].is<const char*>()) {
        String v = o["key2"].as<String>();
        if (v != MASK) tmp.pushChannels[i].key2 = v;
      }
      i++;
    }
  }

  config = tmp;
  saveConfig();
  return "";  // 成功
}

// 主 WiFi 生效凭据：网页配置优先，回退编译宏
void getPrimaryWifi(const char*& ssid, const char*& pass) {
  if (config.wifi1Ssid.length() > 0) {
    ssid = config.wifi1Ssid.c_str();
    pass = config.wifi1Pass.c_str();
  } else {
    ssid = WIFI_SSID;
    pass = WIFI_PASS;
  }
}

// 获取当前设备URL
String getDeviceUrl() {
  if (WiFi.status() != WL_CONNECTED) return "(WiFi未连接)";
  return "http://" + WiFi.localIP().toString() + "/";
}
