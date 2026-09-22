# API 参考手册 — 完整函数索引

---

## 模块: config.cpp — 配置管理

### `void saveConfig()`
**用途**: 将 `config` 全局结构体所有字段写入 NVS。

**NVS Namespace**: `"sms_config"`  
**写入模式**: `false`（读写模式）

**写入的 Key 列表**:
| NVS Key | 类型 | 对应字段 |
|---|---|---|
| `smtpServer` | String | `config.smtpServer` |
| `smtpPort` | Int | `config.smtpPort` |
| `smtpUser` | String | `config.smtpUser` |
| `smtpPass` | String | `config.smtpPass` |
| `smtpSendTo` | String | `config.smtpSendTo` |
| `adminPhone` | String | `config.adminPhone` |
| `webUser` | String | `config.webUser` |
| `webPass` | String | `config.webPass` |
| `numBlkList` | String | `config.numberBlackList` |
| `push{i}en` | Bool | 通道 i 是否启用 |
| `push{i}type` | UChar | 通道 i 推送类型 |
| `push{i}url` | String | 通道 i URL |
| `push{i}name` | String | 通道 i 名称 |
| `push{i}k1` | String | 通道 i 参数1 |
| `push{i}k2` | String | 通道 i 参数2 |
| `push{i}body` | String | 通道 i 自定义模板 |

---

### `void loadConfig()`
**用途**: 从 NVS 加载所有配置到 `config` 结构体。包含旧版本迁移逻辑。

**兼容迁移**: 如果存在旧 key `httpUrl` 且第一个推送通道未启用，自动迁移到通道 1。

**默认值**:
- `smtpPort`: 465
- `webUser`: `"admin"` (`DEFAULT_WEB_USER`)
- `webPass`: `"admin123"` (`DEFAULT_WEB_PASS`)
- 通道名称: `"通道1"` ~ `"通道5"`
- 通道类型: `PUSH_TYPE_POST_JSON` (1)

---

### `bool isPushChannelValid(const PushChannel& ch)`
**用途**: 检查单个推送通道配置是否完整可用。

**校验规则**:

| 推送类型 | 必填字段 |
|---|---|
| POST_JSON / BARK / GET / DINGTALK / FEISHU / CUSTOM | `url` 非空 |
| PUSHPLUS / SERVERCHAN | `key1` 非空 |
| GOTIFY | `url` 非空 **且** `key1` 非空 |
| TELEGRAM | `key1` 非空 **且** `key2` 非空 |

**前提**: `ch.enabled == true`，否则直接返回 false。

---

### `bool isConfigValid()`
**用途**: 检查系统是否有至少一种可用的通知方式。

**返回 true 条件**: 邮件配置完整（4 个 SMTP 字段均非空）**或** 至少一个推送通道通过 `isPushChannelValid()` 校验。

---

### `String getDeviceUrl()`
**返回**: `"http://{WiFi.localIP}/"`

---

## 模块: modem.cpp — 模组控制

### `String sendATCommand(const char* cmd, unsigned long timeout)`
**参数**:
- `cmd`: AT 指令字符串（不含 `\r\n`，函数自动追加）
- `timeout`: 等待超时（毫秒）

**行为**:
1. 清空 Serial1 缓冲区
2. 发送 `cmd\r\n`
3. 等待直到收到 "OK" 或 "ERROR" 或超时
4. 收到 OK/ERROR 后额外 `delay(50)` 读取剩余数据

**返回**: 模组完整响应字符串（含 OK/ERROR）

---

### `void modemPowerCycle()`
**行为**:
1. 配置 `MODEM_EN_PIN` 为输出
2. 拉低 1200ms → 关闭模组
3. 拉高 6000ms → 开启模组并等待启动

**注意**: 调用后需清空 Serial1 缓冲区（该函数不自动清空）

---

### `void resetModule()`
**行为**:
1. 调用 `modemPowerCycle()`
2. 清空 Serial1
3. 循环 10 次尝试 `sendATandWaitOK("AT", 1000)`
4. 打印恢复结果

---

### `bool sendATandWaitOK(const char* cmd, unsigned long timeout)`
**与 sendATCommand 区别**: 仅返回 true/false，不做额外 delay。用于初始化流程中的幂等检查。

---

### `bool waitCEREG()`
**用途**: 轮询 `AT+CEREG?` 直到网络注册成功。

**注册状态码**:
- `1`: 已注册本地网络 → true
- `5`: 已注册漫游 → true
- `0,2,3,4`: 未注册 → false

**超时**: 单次轮询 2000ms（调用方在外层循环调用）

---

### `void blink_short(unsigned long gap_time = 500)`
**行为**: LED 亮 50ms → 灭 → 等待 `gap_time` ms。

---

### `bool sendSMS(const char* phoneNumber, const char* message)`
**流程**:
1. `pdu.setSCAnumber()` 使用默认短信中心
2. `pdu.encodePDU(phoneNumber, message)` 编码
3. 发送 `AT+CMGS=<pduLen>`
4. 等待 `>` 提示符（5 秒超时）
5. 发送 PDU 数据 + `Ctrl+Z` (0x1A)
6. 等待 OK/ERROR（30 秒超时）

**返回**: true=成功, false=PDU编码失败/无提示符/ERROR/超时

### 模组信息缓存（全局变量）

| 变量 | 来源 | 更新时机 |
|---|---|---|
| `modemImeiCache` | `AT+GSN` | 模组初始化注册成功后查一次 |
| `modemIccidCache` | `AT+ICCID` | 模组初始化注册成功后查一次 |
| `modemOperatorCache` | `AT+COPS?` | 初始化后 + 健康巡检每 5 分钟刷新 |
| `modemModelCache` / `modemFwCache` | `ATI` 第 2/3 行 | 模组初始化时 |

供 `/status` 返回，Web「系统概览 → 模组信息」卡片展示；原始查询可走「AT 终端」。

### 共享 AT 响应解析（modem.cpp 导出，多处复用）

| 函数 | 作用 | 使用方 |
|---|---|---|
| `String modemParseCops(const String& resp)` | `AT+COPS?` 响应 → 运营商名（失败空串） | 初始化 / 健康巡检 / `/modem?operator` |
| `bool modemParseCsq(const String& resp, int& dbm)` | `AT+CSQ` 响应 → dBm（rssi=99 返回 false） | 健康巡检 / `/modem?signal` |
| `String modemQueryImei()` | 发送并解析 `AT+GSN`（须持串口占用权） | 初始化 / `/modem?imei` |

---

## 模块: push.cpp — 推送与邮件

### `void sendEmailNotification(const char* subject, const char* body)`
**前提检查**: WiFi 已连接且 SMTP 四个字段均非空，否则打印跳过日志。

**重试机制**: 最多尝试 3 次，失败后递增退避（1s/2s）；每次尝试前 `smtp.stop()` 清理残留连接状态；认证失败属配置错误，不重试直接返回。

**实现**:
1. 创建 `smtp.connect(server, port, callback)`，返回值 false 则重试
2. `smtp.authenticate(user, pass, readymail_auth_password)`，失败则停止重试（配置错误）
3. 构造 `SMTPMessage`，设置 from/to/subject/body/timestamp
4. `smtp.send(msg)`，返回值 false 则重试

**from 格式**: `"sms notify <user@example.com>"`  
**to 格式**: `"your_email <receiver@example.com>"`  
**timestamp**: 使用 `time(nullptr)`（需 NTP 已同步）

---

### `void sendSMSToServer(const char* sender, const char* message, const char* timestamp)`
**行为**:
1. 检查 WiFi 连接
2. 检查是否有启用的有效通道
3. 遍历所有通道，对每个有效通道调用 `sendToChannel()`
4. 通道间 delay(100ms)

---

### `void sendToChannel(const PushChannel& channel, const char* sender, const char* message, const char* timestamp)`
**核心推送函数**。根据 `channel.type` 构建对应的 HTTP 请求参数（URL/ContentType/Body），统一交给 `executeChannelRequest()` 发送（内置重试）。

**签名相关**:
- 钉钉: `HMAC-SHA256(timestamp+"\n"+secret)` → Base64 → URLEncode → 追加到 URL
- 飞书: `HMAC-SHA256(timestamp+"\n"+secret)` → Base64 → 放入 JSON body

**占位符**: 自定义模板（PUSH_TYPE_CUSTOM）支持 `{sender}` `{message}` `{timestamp}` 占位符替换。

---

### `bool executeChannelRequest(...)` — 通道请求执行器（带重试）
**重试策略**: 最多尝试 3 次，失败后递增退避（0.5s / 1s）。

| 结果 | 处理 |
|---|---|
| 连接失败/超时（`httpCode <= 0`） | 记录错误 → 重试 |
| HTTP 2xx 且响应体业务码正确 | 成功返回 true |
| HTTP 4xx（除 429） | 配置问题，立即放弃不重试 |
| HTTP 5xx / 429 / 业务码不符 | 重试 |
| 3 次均失败 | 返回 false，日志提示已丢弃 |

### `bool isBodySuccess(const PushChannel& channel, const String& resp)` — 响应体业务码校验
部分平台无论成败 HTTP 都返回 200，需检查响应体：

| 平台 | 成功标志 |
|---|---|
| 钉钉 | `"errcode":0` |
| 飞书 | `"code":0` |
| PushPlus | `"code":200` |
| Server酱 | `"code":0` |
| Telegram | `"ok":true` |
| 其余平台 | 不检查，以 HTTP 状态码为准 |

---

### `String urlEncode(const String& str)`
标准 URL 编码（空格 → `+`, 非字母数字 → `%XX`）

---

### `String jsonEscape(const String& str)`
JSON 字符串转义：`"` → `\"`, `\` → `\\`, `\n` → `\\n`, `\r` → `\\r`, `\t` → `\\t`

---

### `String dingtalkSign(const String& secret, int64_t timestamp)`
HMAC-SHA256(timestamp + "\n" + secret, secret) → Base64 → URLEncode

---

### `int64_t getUtcMillis()`
通过 `gettimeofday()` 获取 UTC 毫秒级时间戳，失败则回退 `time(nullptr) * 1000`

---

## 模块: sms_process.cpp — 短信处理

### `void initConcatBuffer()`
将 `concatBuffer[MAX_CONCAT_MESSAGES]` 全部标记为未使用，清空所有分段。

---

### `int findOrCreateConcatSlot(int refNumber, const char* sender, int totalParts)`
**查找逻辑**:
1. 遍历缓冲区，找已用且 refNumber+sender 匹配的槽位 → 直接返回
2. 遍历缓冲区，找未使用槽位 → 初始化并返回
3. 无空闲槽位 → 覆盖最老的槽位（按 firstPartTime 比较）

---

### `String assembleConcatSms(int slot)`
按顺序拼接 `concatBuffer[slot].parts[0..totalParts-1]`，缺失分段标记 `[缺失分段N]`

---

### `void clearConcatSlot(int slot)`
将该槽位所有字段复位。

---

### `void checkConcatTimeout()`
遍历所有使用中的槽位，若 `millis() - firstPartTime >= 30000ms`，强制合并转发不完整消息，并清空槽位。

---

### `String readSerialLine(HardwareSerial& port)`（已改为 sms_process.cpp 文件内静态函数）
不再对外导出：内部使用跨调用静态缓冲（非重入），外部误用会破坏 URC 解析状态。
逐字节读取串口，遇 `\n` 返回行，`\r` 跳过，超长行保护（超过 SERIAL_BUFFER_SIZE 归零），无完整行返回空串。

**注意**: 使用 `static` 缓冲区，仅适合单线程调用。

---

### `bool isHexString(const String& str)`
检查字符串是否全为十六进制字符 `[0-9A-Fa-f]`。

---

### `bool isInNumberBlackList(const char* sender)`
**匹配逻辑**: 逐行读取 `config.numberBlackList`（换行符分隔），支持 `+86` 前缀的模糊匹配：
- `+8613800138000` 匹配黑名单中的 `13800138000`
- `13800138000` 匹配黑名单中的 `+8613800138000`

---

### `bool isAdmin(const char* sender)`
比较发送者与 `config.adminPhone`，忽略 `+86` 前缀差异。

---

### `void processAdminCommand(const char* sender, const char* text)`
**支持命令**:
| 命令格式 | 行为 |
|---|---|
| `SMS:号码:内容` | 调用 `sendSMS()` 代发短信，邮件通知结果 |
| `RESET` | 发送通知邮件 → `resetModule()` → `ESP.restart()` |

---

### `void processSmsContent(const char* sender, const char* text, const char* timestamp)`
**处理顺序**:
1. `isInNumberBlackList()` → 忽略
2. `isAdmin()` + 命令格式检测 → `processAdminCommand()` → 不再发普通通知
3. `sendSMSToServer()` — 推送所有启用的通道
4. `sendEmailNotification()` — 邮件通知

---

### `void checkSerial1URC()`
**状态机**: `IDLE` ↔ `WAIT_PDU`

IDLE 状态检测 `+CMT:` 行 → 转入 WAIT_PDU。
WAIT_PDU 状态读取 PDU hex 数据 → `pdu.decodePDU()` 解析 → 根据 `concatInfo` 判断长短信/普通短信 → 恢复 IDLE。

---

## 模块: web_handlers.cpp — HTTP 处理

### 日志系统 API

### `void logCapture(const String& msg)`
### `void logCapture(const char* msg)`
**用途**: 输出日志，但不换行。内容追加到行缓冲区 `_logLine`，不会立即写入环形缓冲区。同时输出到 `Serial`。

**使用场景**: 与 `logCaptureLn()` 配合，构建一行完整日志：
```cpp
logCapture("目标号码: ");
logCaptureLn(String(phoneNumber));
// 环形缓冲区中只有一行: "目标号码: 13800138000"
```

---

### `void logCaptureLn(const String& msg)`
### `void logCaptureLn(const char* msg)`
**用途**: 输出日志并换行。将 `_logLine` + msg 提交到环形缓冲区，然后清空行缓冲区。同时输出到 `Serial.println`。

**注意**: 环形缓冲区每调用一次 `logCaptureLn` 即可产生一行日志，因此 `logCapture()` 和 `logCaptureLn()` 的日志会在环形缓冲区中合并为同一行。

---

### `void logCaptureF(const char* fmt, ...)`
**用途**: 格式化日志输出（类似 `printf`）。支持 `%d`/`%s`/`%f` 等格式。如果格式化字符串以 `\n` 结尾，自动提交整行到缓冲区。

---

### `void handleLog()`
**用途**: HTTP 端点 `GET /log`，返回环形缓冲区中所有日志行。

**返回格式**: `application/json`
```json
["行1", "行2", "行3", ...]
```
**鉴权**: 需要 HTTP Basic Auth。最多返回 120 行（环形缓冲区容量）。

---

### `bool checkAuth()`
HTTP Basic Authentication，账号密码来自 `config.webUser` / `config.webPass`。

---

### `void handleRoot()`
返回 SPA 主页 HTML。通过 `html.replace("%KEY%", value)` 替换模板占位符，动态生成 5 个推送通道的表单。页面包含 10 个可切换面板（系统概览/账号管理/邮件通知/推送通道/管理员黑名单/发送短信/模组诊断/网络测试/模组控制/AT终端/系统日志），通过侧边栏 JS 切换显示。

**模板占位符**:

| 占位符 | 数据来源 |
|---|---|
| `%IP%` | `WiFi.localIP().toString()` |
| `%WEB_USER%` / `%WEB_PASS%` | `config.webUser` / `config.webPass` |
| `%SMTP_SERVER%` ~ `%SMTP_SEND_TO%` | `config.smtp*` |
| `%ADMIN_PHONE%` | `config.adminPhone` |
| `%NUMBER_BLACK_LIST%` | `config.numberBlackList` |
| `%SMTP_CHECK%` | 邮件配置是否完整 |
| `%PUSH_COUNT%` | 已启用的有效推送通道数 |
| `%PUSH_CHANNELS%` | 循环生成 5 个通道的 HTML 表单 |

---

### `void handleToolsPage()`
兼容旧链接，直接委托给 `handleRoot()` 返回同一 SPA 页面。

---

### `void handleSave()`
解析 POST 表单 → 写入 `config` → `saveConfig()` → 重新校验 → 返回 JSON。
注意：**webUser/webPass 留空时保留旧值**（不再回退默认密码）；推送类型/SMTP 端口做范围校验，非法值回退默认。

---

### `void handleFlightMode()`
根据 `?action=` 控制飞行模式：

| action | AT 指令 | 说明 |
|---|---|---|
| `query` | `AT+CFUN?` | 查询当前 CFUN 值 |
| `toggle` | 查询 + `AT+CFUN=1/4` | 1↔4 切换 |
| `on` | `AT+CFUN=4` | 强制开启 |
| `off` | `AT+CFUN=1` | 强制关闭 |

---

### `void handleATCommand()`
通过 `?cmd=` 透传 AT 指令到 `sendATCommand()`，返回 JSON `{success, message}`。

---

### `void handleSendSms()`
解析 POST 表单 phone/content → 调用 `sendSMS()` → 返回 HTML 结果页（3 秒跳转）。

---

### `void handlePing()`
**流程**:
1. `AT+CGACT=1,1` 激活数据连接
2. `AT+MPING="8.8.8.8",30,1` ping 一次（30 秒超时）
3. 解析 `+MPING:` URC 响应，提取 IP/延迟/TTL
4. `AT+CGACT=0,1` 关闭数据连接
5. 返回 JSON `{success, message}`

**注意**: 整个操作最长约 35 秒。

---

### `void handleSystem()`
系统控制命令，路由 `GET /system`。

| action | 行为 |
|---|---|
| `restart` | 先返回 JSON 成功响应 → `delay(500)` 确保发出 → `ESP.restart()` 整机重启 |
| 其他 | 返回 `{"success":false,"message":"未知操作"}` |

**注意**: 重启后设备需重新走完整启动流程（WiFi + 模组初始化），约 1 分钟恢复。

---

### `void handleStatus()`
路由 `GET /status`，概览页 5 秒轮询的轻量状态 JSON：

```json
{
  "ip": "192.168.1.x", "ssid": "...", "heap": 187, "uptime": "3:42:17",
  "modem": true, "signal": "-71 dBm",
  "operator": "中国移动", "imei": "86...", "iccid": "8986...",
  "model": "ML307R", "fw": "ML307RAR01A07",
  "smsOnly": true, "email": true, "push": 2
}
```

`signal` 来自 `health.cpp` 巡检缓存，`operator/imei/iccid/model/fw` 来自 `modem.cpp` 信息缓存。

### `void handleJobStatus()`
路由 `GET /job?id=N`，任务队列状态轮询：`{"state":"queued|running|done|unknown","success":bool,"message":"..."}`。
Ping/发短信/AT/模组重启均入队执行（jobs.cpp），浏览器不挂连接。

### `void handleRecordsExport()`
路由 `GET /recordsexport`，流式导出短信记录 CSV（全量历史，UTF-8 BOM）。

### `void handleConfigExport()` / `void handleConfigImport()`
`GET /config/export[?plain=1]` 导出配置 JSON（默认密钥打码 `******`，plain=1 含明文）；
`POST /config/import`（body 为 JSON）校验后整体应用并保存，打码字段跳过，非法值整体拒绝。

### jobs 模块（jobs.h/.cpp）
`jobsSubmit(type, arg1, arg2)` 入队（忙返回 0）；`jobsQuery(id, out)` 查询；`jobsRun()` 在 loop 中执行；
`jobsIdle()` 供健康巡检避让。任务类型见 task_types.h 的 ModemJobType。

### push 模块新增
`pushTypeCount()/pushTypeName()` — 描述符表访问；`pushChannelFieldsValid()` — 表驱动字段校验；
`pushChannelCooling(i)/pushChannelStatsJson()/pushStatsNoteBlocked(i)` — 通道健康熔断；
`pushBroadcastText(title, text)` — 文本广播到全通道+邮件（每日报告用）。

### health 模块新增
`HealthDayStats dayStats` — 每日统计（收信/转发/拦截/信号范围/堆最低）；
`healthSendReport()` — 立即发送每日报告并清零统计（每天 8 点自动触发，或 REPORT 命令）。
