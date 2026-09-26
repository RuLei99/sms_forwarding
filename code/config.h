#ifndef CONFIG_H
#define CONFIG_H

#include "globals.h"

void saveConfig();
void loadConfig();
// 配置导出为 JSON（maskSecrets=true 时密钥字段打码为 ******）
String configToJson(bool maskSecrets);
// 从 JSON 恢复配置（打码字段跳过）。成功返回空串，失败返回错误说明
String configFromJson(const String& json);
bool isPushChannelValid(const PushChannel& ch);
bool isConfigValid();
// SIM PIN：空表示不自动解锁；非空时必须为 4-8 位纯数字
bool isSimPinValid(const String& pin);
String getDeviceUrl();
// 主 WiFi 生效凭据：网页配置(NVS)优先，留空回退 wifi_config.h 编译宏
void getPrimaryWifi(const char*& ssid, const char*& pass);
// 备用 WiFi 生效凭据：网页配置(NVS)优先，回退编译宏；两者皆空时返回空串（无备网）
void getBackupWifi(const char*& ssid, const char*& pass);

#endif
