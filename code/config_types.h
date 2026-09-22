#ifndef CONFIG_TYPES_H
#define CONFIG_TYPES_H

#include <Arduino.h>

// 推送通道类型
enum PushType {
  PUSH_TYPE_NONE = 0,      // 未启用
  PUSH_TYPE_POST_JSON = 1, // POST JSON格式 {"sender":"xxx","message":"xxx","timestamp":"xxx"}
  PUSH_TYPE_BARK = 2,      // Bark格式 POST {"title":"xxx","body":"xxx"}
  PUSH_TYPE_GET = 3,       // GET请求，参数放URL中
  PUSH_TYPE_DINGTALK = 4,  // 钉钉机器人
  PUSH_TYPE_PUSHPLUS = 5,  // PushPlus
  PUSH_TYPE_SERVERCHAN = 6,// Server酱
  PUSH_TYPE_CUSTOM = 7,    // 自定义模板
  PUSH_TYPE_FEISHU = 8,    // 飞书机器人
  PUSH_TYPE_GOTIFY = 9,    // Gotify
  PUSH_TYPE_TELEGRAM = 10, // Telegram Bot
  PUSH_TYPE_NTFY = 11,     // ntfy（自建/官方推送服务）
  PUSH_TYPE_MQTT = 12      // MQTT publish（Home Assistant 等联动）
};

// 最大推送通道数
#define MAX_PUSH_CHANNELS 5

// 推送通道配置（通用设计，支持多种推送方式）
struct PushChannel {
  bool enabled;           // 是否启用
  PushType type;          // 推送类型
  String name;            // 通道名称（用于显示）
  String url;             // 推送URL（webhook地址）
  String key1;            // 额外参数1（如：钉钉secret、pushplus token等）
  String key2;            // 额外参数2（备用）
  String customBody;      // 自定义请求体模板（使用 {sender} {message} {timestamp} 占位符）
};

// 配置参数结构体
struct Config {
  String smtpServer;
  int smtpPort;
  String smtpUser;
  String smtpPass;
  String smtpSendTo;
  String adminPhone;
  PushChannel pushChannels[MAX_PUSH_CHANNELS];  // 多推送通道
  String webUser;      // Web管理账号
  String webPass;      // Web管理密码
  String numberBlackList;  // 号码黑名单（换行符分隔）
  bool smsOnly;        // 仅收短信模式：锁定模组数据连接（去激活PDP），Ping被禁用，防止漫游流量扣费
  bool filterWhitelist;    // 关键词过滤模式：true=白名单（仅转发命中），false=黑名单（拦截命中）
  String filterKeywords;   // 过滤关键词，每行一个；留空=不过滤
  int tzHours;             // 时区（小时，-12~14）：每日报告触发时间用，默认 8（北京时间）
  bool reportEnabled;      // 每日 8 点健康报告（邮件 + 所有有效推送通道），默认开
  String wifi1Ssid;        // 主 WiFi SSID（空=使用固件内置 wifi_config.h 宏）
  String wifi1Pass;        // 主 WiFi 密码
  String wifi2Ssid;        // 备用 WiFi SSID（空=不启用双 WiFi 热备）
  String wifi2Pass;        // 备用 WiFi 密码
};

// 默认Web管理账号密码
#define DEFAULT_WEB_USER "admin"
#define DEFAULT_WEB_PASS "admin123"

// 默认 SMTP 端口（加载/保存/前端占位符统一引用，避免魔法数字散落）
#define DEFAULT_SMTP_PORT 465

// Web 表单可保存的推送类型范围（与 PushType 枚举对应）
#define PUSH_TYPE_MIN 1
#define PUSH_TYPE_MAX 12

// 长短信合并相关定义
#define MAX_CONCAT_PARTS 10       // 最大支持的长短信分段数
#define CONCAT_TIMEOUT_MS 30000   // 长短信等待超时时间(毫秒)
#define MAX_CONCAT_MESSAGES 5     // 最多同时缓存的长短信组数

// 长短信分段结构
struct SmsPart {
  bool valid;           // 该分段是否有效
  String text;          // 分段内容
};

// 长短信缓存结构
struct ConcatSms {
  bool inUse;                           // 是否正在使用
  int refNumber;                        // 参考号
  String sender;                        // 发送者
  String timestamp;                     // 时间戳（使用第一个收到的分段的时间戳）
  int totalParts;                       // 总分段数
  int receivedParts;                    // 已收到的分段数
  unsigned long firstPartTime;          // 收到第一个分段的时间
  SmsPart parts[MAX_CONCAT_PARTS];      // 各分段内容
};

#endif
