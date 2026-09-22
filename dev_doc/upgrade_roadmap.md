# 升级路线图：10 个候选升级方案

> 2026-09 评审基线：commit `4a1ef8b` + 稳定性修复批次（30 项）。
> 每项含：现状痛点 → 方案设计 → 涉及文件 → 工作量（S<半天 / M≈1-2天 / L>2天）→ 风险 → 验收标准。
> **实施状态（2026-09-23）**：1 OTA、2 认证加固、9 规则引擎 — 按决策不做/暂缓。
> 已实施：3 任务队列（轻量单槽版）、4 记录持久化+CSV 导出、5 配置备份恢复、6 通道健康熔断、7 每日报告（邮件+全通道推送+REPORT 命令）、8 硬件看门狗、10 通道插件化+ntfy/MQTT、时区正确化、双 WiFi 热备。

---

## 1. OTA 固件升级（Web 端直接刷机）

**痛点**：每次改代码都要拆外壳接 USB 线烧录。设备通常装在弱电箱/窗边（4G 信号考虑），维护成本高。

**方案**：
- 用 ESP32 自带 `Update.h` + `WebServer` 的 `/update` POST 路由接收 `.bin`（`multipart/form-data`）。
- Web UI「系统控制」面板加"上传固件"入口（复用现有 result-box 反馈组件）。
- ESP32 OTA 分区方案天生双备份：新固件写坏不影响旧分区启动，变砖风险极低。
- 上传完成后 `ESP.restart()`，前端复用现有 30 秒自动刷新逻辑（`scheduleSysReload`）。

**涉及**：`code.ino`（路由）、`web_handlers.cpp`（handler）、`web_html.cpp`（UI）、分区表确认（默认双 OTA 分区即可）。

**工作量**：S-M ｜ **风险**：低（需 Basic Auth 保护该路由，已具备）

**验收**：Web 上传 1.2MB bin → 自动重启 → 新版本日志与 UI 生效；上传中断电后旧固件正常启动。

---

## 2. Web 认证加固（防暴力破解 + 会话令牌）

**痛点**：HTTP Basic 明文传输；默认 admin/admin123；无登录失败限制。设备暴露公网（端口映射）时弱密码 10 分钟内可被爆破。

**方案**（渐进式，不做 TLS——WebServer 库不支持，上 TLS 需换框架得不偿失）：
1. 登录失败计数：NVS 记录连续失败次数，≥5 次锁定 5 分钟（`/login` 统一入口）。
2. 会话 token：登录成功发随机 token（Cookie），后续接口验 token，替代每次 Basic 明文。
3. 首次强制改密：`webPass == DEFAULT_WEB_PASS` 时 Web 界面只开放改密页。
4. 文档建议公网场景走 Cloudflare Tunnel / 反向代理加 TLS。

**涉及**：`web_handlers.cpp`（checkAuth 重构）、`config_types.h`/`config.cpp`（失败计数字段）、`web_html.cpp`（登录页）。

**工作量**：M ｜ **风险**：中（认证改造需仔细回归所有路由）

**验收**：错误密码 5 次后拒绝登录 5 分钟；默认密码登录被引导改密；token 过期后重新登录。

---

## 3. 模组操作任务队列（真正的异步化）

**痛点**：`task_types.h` 已定义 `ModemJob`/`ModemResult` 结构但从未接线。当前 Ping（35s）、模组重启（15s+）、`modemInit`（最长约 1 分钟）全部在 HTTP handler / loop 里同步执行，期间靠嵌套 `handleClient()` 苟活，429 "模组正忙" 频繁出现；`sendSMS` 30 秒等待期间新短信 URC 只能堆在串口缓冲。

**方案**：
- loop 中增加任务泵：`pendingJob` 单槽（足够，模组本来就是串行设备）+ 状态机推进。
- Web 请求（ping/restart/sendsms/at）入队后立即返回 `{queued:true, jobId}`，前端轮询 `/job?id=` 获取进度/结果。
- AT 等待循环改为每 tick 只读非阻塞、`yield()` 交替跑 `healthTask`/URC 解析，彻底消灭长阻塞。

**涉及**：`task_types.h`（启用）、`code.ino`（loop 泵）、`web_handlers.cpp`（入队+轮询）、`modem.cpp`（等待循环状态机化）、`web_html.cpp`（前端轮询）。

**工作量**：L ｜ **风险**：高（核心控制流重构，需充分台架测试）

**验收**：Ping 期间网页所有面板可操作无卡顿；`/status` 轮询连续；新短信在模组操作中不丢失。

---

## 4. 短信记录持久化与导出

**痛点**：`records.cpp` 50 条纯内存环形缓冲，重启即清空；想查"上周的验证码"不可能。

**方案**：
- LittleFS（`FS.h`，ESP32 核心 builtin）存 JSONL 追加文件，`recordsAdd` 时同时落盘（写前检查文件 >256KB 则轮转 `records.1.jsonl`）。
- NOR flash 磨损预算：按每天 50 条 × 4KB 擦写块估算，寿命 >10 年；可配置"仅持久化转发成功的"。
- Web「短信记录」加"导出 CSV"按钮（`Content-Disposition: attachment`）。

**涉及**：`records.cpp/h`、`web_handlers.cpp`（导出路由）、`web_html.cpp`（按钮）、`code.ino`（`LittleFS.begin`）。

**工作量**：M ｜ **风险**：低-中（flash 写入在 loop 路径，注意改为攒批）

**验收**：重启后记录保留；导出 CSV 用 Excel 打开中文不乱码（UTF-8 BOM）。

---

## 5. 配置备份 / 恢复

**痛点**：换设备或重刷固件后要手填 20+ 表单字段（10 种推送通道 × URL/密钥）。密钥抄写易错。

**方案**：
- 导出：`GET /config/export` 生成 JSON（密钥可选打码，`?plain=1` 才含明文）。
- 导入：`POST /config/import` 校验字段合法性（复用现有 clamp 逻辑）→ 写 NVS → 重启生效。
- 前端「账号管理」面板加"下载配置/恢复配置"按钮 + 文件选择。

**涉及**：`config.cpp`（serialize/deserialize）、`web_handlers.cpp`（两个路由）、`web_html.cpp`。

**工作量**：S-M ｜ **风险**：低（导入校验失败必须原子拒绝，不得半写）

**验收**：导出→恢复→所有通道推送测试通过；非法 JSON 拒绝且原配置无损。

---

## 6. 推送通道健康统计与熔断退避

**痛点**：某通道 token 失效后，每条短信仍要经历 3 次重试 × 超时（约 15s），拖慢整体转发，其他通道也被 `delay(100)` 拖累；用户不登录根本不知道通道挂了。

**方案**：
- 每通道维护：`okCnt/failCnt/consecFails`（内存 + `/status` 输出）。
- 熔断：连续失败 ≥5 次标记"降级"，跳过发送 30 分钟（半开探测一条）；恢复成功即复位。
- 概览卡片显示各通道成功率；降级时状态横幅告警。
- 顺手把 `sendSMSToServer` 的通道间 `delay(100)` 改为仅在上一通道失败时延迟。

**涉及**：`config_types.h`（运行时统计结构）、`push.cpp`、`web_handlers.cpp`（/status 扩展）、`web_html.cpp`（展示）。

**工作量**：M ｜ **风险**：低

**验收**：拔掉某通道 webhook，5 条短信后该通道被熔断，总转发耗时回落，横幅出现告警。

---

## 7. 每日健康报告邮件

**痛点**：设备静默故障（WiFi 夜间掉线 2 小时、SIM 停机）只能靠用户主动开页面发现。

**方案**：
- `health.cpp` 增加日统计：收信数、转发成功率（复用 6 的计数）、信号 min/max、重启次数（RTC 计数）、堆内存最低值。
- 每天 08:00（NTP 已后台补同步）发一封摘要邮件；当天 0 条短信也发（"心跳"意义）。
- 管理员短信命令增加 `REPORT` 立即触发。

**涉及**：`health.cpp/h`、`push.cpp`（复用 sendEmailNotification）、`sms_process.cpp`（命令）。

**工作量**：S-M ｜ **风险**：低

**验收**：次日早 8 点收到昨日汇总；`REPORT` 命令即时回邮。

---

## 8. 硬件看门狗 + loop 卡死自愈

**痛点**：目前任务看门狗未显式配置。若模组固件异常导致 `sendATCommand` 死循环之外的新路径卡死（如 SSL 卡在不返回的握手），loop 停转且永不复位，只能人工断电。

**方案**：
- `esp_task_wdt_init(30, true)` + loop 任务注册喂狗；长操作（邮件/推送/模组）前 `esp_task_wdt_reset()` 或改用 3 的任务泵后天然解决。
- 保守起步：WDT 超时 30s（覆盖最长合法操作 Ping 35s 需拆分为分段喂狗）。
- 结合已加的 `esp_reset_reason()` 启动日志，复位原因会体现在系统日志里形成闭环。

**涉及**：`code.ino`（setup/loop）、`push.cpp`/`modem.cpp`（长循环内喂狗点）。

**工作量**：S ｜ **风险**：中（超时参数需覆盖所有合法长操作，否则误复位；建议先 60s 观察一周）

**验收**：人为构造死循环（测试固件）30-60s 内自动复位并留下复位原因日志；正常运行一周零误复位。

---

## 9. 关键词规则引擎升级（正则 + 按通道路由）

**痛点**：现有关键词过滤是全文 `indexOf`：无法表达"1069 开头的号码"、"含【】的营销短信"、验证码类走 Bark、账单类走邮件这类差异化需求。

**方案**：
- 规则结构升级为列表：`匹配模式（前缀/包含/正则）+ 动作（丢弃/转发到指定通道组合）`。
- 正则用轻量实现（ESP32 核心 `regex.h` std::regex 体积大，建议内置 slre 约 2KB）；每条短信最多评估 N 条规则，超时 50ms 放行（fail-open，转发优先）。
- UI：「管理员 & 黑名单」面板改规则表格（增删行）。

**涉及**：`config_types.h`、`sms_process.cpp`、`config.cpp`（NVS 新键）、`web_html.cpp`、`web_handlers.cpp`。

**工作量**：M-L ｜ **风险**：中（正则超时保护必须可靠，防止恶意短信构造灾难回溯）

**验收**：`1069*` 规则拦截营销短信；`验证码` 规则只推 Bark；无规则时行为与现版本完全一致（向后兼容旧关键词配置）。

---

## 10. 推送通道插件化 + ntfy/MQTT 新通道

**痛点**：新增一种推送要同步改 4 处（`PushType` 枚举、`sendToChannel` case、`isPushChannelValid`、`web_html` UI + JS hint），本次审计中该耦合已造成固件/前端/预览三份类型表漂移的隐患。

**方案**：
- 定义通道描述符表（单一天事实来源）：`{类型, 名称, 需要URL?, 需要key1?, key1含义, 默认URL, 请求构造函数}`。
- `sendToChannel`/`isPushChannelValid`/`handleRoot` 选项/前端 hint 全部查表生成（前端 hint 可由 `/status` 或模板注入 JSON 生成，消灭 JS 里的硬编码表）。
- 在新架构上顺手加两种高需求通道：**ntfy**（自建推送，家庭服务器场景）、**MQTT publish**（Home Assistant 联动）。

**涉及**：`config_types.h`、`push.cpp`（描述符表+2 通道）、`web_handlers.cpp`、`web_html.cpp`、`tools/preview_server.py`（预览同步查表）。

**工作量**：M ｜ **风险**：中（重构 sendToChannel 需保持现有 10 通道行为逐通道回归）

**验收**：现有 10 通道推送测试全部通过；新增 ntfy 通道 30 分钟内可上线；添加新通道只需 1 个描述符 + 1 个构造函数。

---

## 其他候选（未进前 10）

- **时区正确化**：PDU SCTS 时间戳按本地时区解析（现为网络原始时间）。
- **Web UI 深色模式**：`prefers-color-scheme` 媒体查询，纯前端改动。
- **双 WiFi 热备**：主 SSID 连不上自动切备用 SSID（需扩 wifi_config 为数组 + NVS）。
- **短信静默时段**：夜间不推送只记录（规则引擎的简化子集）。
