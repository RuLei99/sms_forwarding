#include "modem.h"
#include "web_handlers.h"
#include "config.h"
#include "sms_process.h"
#include <esp_task_wdt.h>

// 模组信息缓存（Web 概览展示，见 modem.h）
String modemImeiCache = "";
String modemOperatorCache = "";
String modemIccidCache = "";
String modemModelCache = "";
String modemFwCache = "";

// ---- 共享 AT 响应解析（初始化 / 健康巡检 / Web 查询共用，避免各处复制粘贴） ----

// 从 AT+COPS? 响应中提取运营商名（带引号的名称段），失败返回空串
String modemParseCops(const String& resp) {
  int q1 = resp.indexOf('"');
  int q2 = resp.indexOf('"', q1 + 1);
  if (q1 >= 0 && q2 > q1) return resp.substring(q1 + 1, q2);
  return "";
}

// 从 AT+CSQ 响应中解析 dBm。成功返回 true 并写 dbm（rssi=99 未知也返回 false）
bool modemParseCsq(const String& resp, int& dbm) {
  int csqIdx = resp.indexOf("+CSQ:");
  if (csqIdx < 0) return false;
  int commaIdx = resp.indexOf(',', csqIdx);
  if (commaIdx < 0) return false;
  int rssi = resp.substring(csqIdx + 5, commaIdx).toInt();
  if (rssi < 0 || rssi == 99) return false;
  dbm = -113 + rssi * 2;
  return true;
}

// 发送 AT+GSN 并解析 IMEI（调用方必须已持有串口占用权）。失败返回空串
String modemQueryImei() {
  String resp = sendATCommand("AT+GSN", 3000);
  resp.trim();
  int okIdx = resp.lastIndexOf("OK");
  if (okIdx > 0) resp = resp.substring(0, okIdx);
  int echoIdx = resp.indexOf("AT+GSN");
  if (echoIdx >= 0) resp = resp.substring(echoIdx + 6);
  resp.trim();
  if (resp.length() > 0 && resp.indexOf("ERROR") < 0) return resp;
  return "";
}

// 发送AT命令并获取响应
String sendATCommand(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  
  unsigned long start = millis();
  String resp = "";
  String tail = "";
  resp.reserve(256);
  while (millis() - start < timeout) {
    wdtFeed();  // 长等待喂狗（sendATCommand 是多数慢操作的底层）
    if (Serial1.available()) {
      char c = Serial1.read();
      // 串口接错/电平异常时可能持续产生噪声。限制返回体，避免 String 无限增长耗尽堆；
      // 另用短滑动窗口识别位于截断点之后的 OK/ERROR。
      if (resp.length() < 8192) resp += c;
      tail += c;
      if (tail.length() > 32) tail.remove(0, tail.length() - 32);
      if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0 ||
          tail.indexOf("OK") >= 0 || tail.indexOf("ERROR") >= 0) {
        // 读取剩余数据（最多 50ms）
        unsigned long t = millis();
        while (millis() - t < 50) {
          if (Serial1.available()) {
            char extra = Serial1.read();
            if (resp.length() < 8192) resp += extra;
          }
          server.handleClient();
        }
        if (resp.length() >= 8192) resp += "\r\n<AT_RESPONSE_TRUNCATED>";
        // AT 执行期间到达的 +CMTI 会被本函数当噪声读走；捞回索引供空闲时补读
        smsNoteCmtiFromResponse(resp);
        return resp;
      }
    }
    server.handleClient();
  }
  smsNoteCmtiFromResponse(resp);
  return resp;
}

// 新增"模组断电重启"函数
// lowMs：EN 拉低时长。开机上电用默认 1200（与原版时序一致）；故障恢复用
// 5000（深断电，供电电容放得更彻底，浅断电对"失聪"态实测救不回）
void modemPowerCycle(unsigned long lowMs) {
  pinMode(MODEM_EN_PIN, OUTPUT);

  logCaptureLn(String("EN 拉低：关闭模组"));
  digitalWrite(MODEM_EN_PIN, LOW);
  delay(lowMs);

  logCaptureLn(String("EN 拉高：开启模组"));
  digitalWrite(MODEM_EN_PIN, HIGH);
  delay(6000);  // 等模组完全启动再发AT（关键）
}

// 重启模组（EN引脚断电重启 + 重新初始化）
// 内部占用 Serial1 期间会嵌套 server.handleClient()，必须持有 modemPortBusy，
// 否则嵌套进来的请求会与初始化 AT 序列抢串口（setup/后台重试路径同样受益）
void resetModule() {
  modemPortBusy = true;
  logCaptureLn(String("正在硬重启模组（EN 深断电重启）..."));
  modemPowerCycle(5000);
  modemInit();
  modemPortBusy = false;
}

// 模组 AT 初始化流程（setup 中调用，resetModule 后也调用）
// 返回是否初始化成功（网络已注册）。失败时置 modemReady=false，
// 不再无限重试 —— 网页保持可用，后台由 healthTask 周期性重试
static bool modemInitInner();  // 实际初始化流程（由 modemInit 包上串口互斥后调用）

// 与原版固件保持一致：ML307R UART 固定使用 115200 8N1。
// 不在运行中扫描/切换波特率，避免部分 ESP32 Core 下 updateBaudRate 后 UART 状态异常。
static bool modemDetectBaud() {
  String resp = sendATCommand("AT", 1000);
  if (resp.indexOf("OK") >= 0 && resp.indexOf("ERROR") < 0) {
    return true;
  }
  logCaptureLn(String("⚠️ 固定115200波特率未收到AT响应"));
  return false;
}

bool modemInit() {
  // 初始化全程占用 Serial1（内部嵌套 handleClient），与其他模组请求互斥
  modemPortBusy = true;
  bool ok = modemInitInner();
  modemPortBusy = false;
  return ok;
}

static bool modemInitInner() {
  // 清掉上电噪声/残留
  while (Serial1.available()) Serial1.read();

  modemReady = false;
  // 清空上次会话的模组信息缓存，避免重初始化失败时页面残留旧 IMEI/运营商
  modemImeiCache = "";
  modemOperatorCache = "";
  modemIccidCache = "";
  bool baudReady = modemDetectBaud();
  int atRetry = 0;
  while (!baudReady && !sendATandWaitOK("AT", 1000)) {
    if (++atRetry >= 20) {
      logCaptureLn(String("⚠️ 模组无响应（已尝试20次），放弃初始化，稍后自动重试"));
      return false;
    }
    logCaptureLn(String("AT未响应，重试..."));
    blink_short();
  }
  logCaptureLn(String("模组AT响应正常"));

  // ---- SIM 状态 / PIN 解锁 ----
  // 安全约束：本次开机周期内最多尝试一次，失败置 simPinUnlockFailed 锁存 ——
  // health 巡检会周期性重跑 modemInit，不锁存的话错误 PIN 每 2 分钟重试一次，
  // 累计 3 次就把卡锁成 PUK。日志不回显 PIN/原始响应（响应里含命令回显）。
  // 断电冷启动后 SIM 初始化常需数秒：未就绪时等 3 秒重查（最多 4 次），而不是直接判死
  String cpin;
  for (int cpinTry = 0; cpinTry < 4; cpinTry++) {
    cpin = sendATCommand("AT+CPIN?", 2000);
    if (cpin.indexOf("READY") >= 0 || cpin.indexOf("SIM PIN") >= 0 ||
        cpin.indexOf("SIM PUK") >= 0) {
      break;
    }
    if (cpinTry < 3) {
      logCaptureLn(String("SIM 未就绪，3 秒后重查（第 ") + String(cpinTry + 1) + "/3 次)...");
      delay(3000);
    }
  }
  if (cpin.indexOf("SIM PUK") >= 0) {
    logCaptureLn(String("⚠️ SIM 卡已被锁死（需 PUK），请放回手机用 PUK 解锁后再用"));
    return false;
  } else if (cpin.indexOf("SIM PIN") >= 0) {
    if (simPinUnlockFailed) {
      logCaptureLn(String("⚠️ PIN 解锁本次开机已失败过，停止自动尝试（防锁卡）；请核对后在网页重新保存 PIN"));
      return false;
    }
    if (isSimPinValid(config.simPin) && config.simPin.length() > 0) {
      logCaptureLn(String("SIM 卡需要 PIN 码，尝试解锁（仅一次）..."));
      String cmd = "AT+CPIN=\"" + config.simPin + "\"";
      String pinResp = sendATCommand(cmd.c_str(), 5000);
      if (pinResp.indexOf("OK") >= 0 && pinResp.indexOf("ERROR") < 0) {
        logCaptureLn(String("SIM PIN 解锁成功"));
      } else {
        simPinUnlockFailed = true;
        logCaptureLn(String("⚠️ SIM PIN 解锁失败，本次开机不再尝试！请核对 PIN 后在网页重新保存（连续错误会锁卡需 PUK）"));
        return false;
      }
    } else {
      logCaptureLn(String("⚠️ SIM 卡需要 PIN，但网页未配置有效 PIN；请先用手机关闭 SIM 锁或填写 PIN"));
      return false;
    }
  } else if (cpin.indexOf("READY") >= 0) {
    logCaptureLn(String("SIM 卡就绪（无需 PIN）"));
  } else {
    logCaptureLn(String("⚠️ SIM 卡多次查询未就绪（已重试 4 次），请检查插卡方向/卡座接触"));
    return false;
  }

  //判断型号，做一些特定操作
  bool need_set_CGACT = true;
  String resp = sendATCommand("ATI", 2000);
  logCaptureLn(String("ATI响应: " + resp));
  if (resp.indexOf("OK") >= 0) {
    // 解析ATI响应
    String manufacturer = "未知";
    String model = "未知";
    String version = "未知";

    // 按行解析
    int lineStart = 0;
    int lineNum = 0;
    for (int i = 0; i < resp.length(); i++) {
      if (resp.charAt(i) == '\n' || i == resp.length() - 1) {
        String line = resp.substring(lineStart, i);
        line.trim();
        if (line.length() > 0 && line != "ATI" && line != "OK") {
          lineNum++;
          if (lineNum == 1) manufacturer = line;
          else if (lineNum == 2) model = line;
          else if (lineNum == 3) version = line;
        }
        lineStart = i + 1;
      }
    }
    //这个模组这条命令有bug
    if(model == "ML307Y") need_set_CGACT = false;
    modemModelCache = model;
    modemFwCache = version;
  }

  if(need_set_CGACT) {
    int cgactRetry = 0;
    while (!sendATandWaitOK("AT+CGACT=0,1", 5000)) {
      if (++cgactRetry >= 3) {
        logCaptureLn(String("⚠️ 设置CGACT失败3次，跳过（不影响收短信）"));
        break;
      }
      logCaptureLn(String("设置CGACT失败，重试..."));
      blink_short();
    }
    if (cgactRetry < 3) logCaptureLn(String("已禁用数据连接(AT+CGACT=0,1)，防止流量消耗"));
  } else {
    logCaptureLn(String("该型号无法配置(AT+CGACT=0,1)，跳过该命令，会不会消耗流量？自求多福"));
  }
  int cnmiRetry = 0;
  // 2,1=存入存储并上报+CMTI（而非 2,2 直推）：ML307R-DC 直推报文在模组发射时
  // 会被串口噪声破坏（实测吞头/乱码洪水，短信直接丢失）；落存储后即使 URC 丢了，
  // 定时扫描兜底也能把短信捞回来
  while (!sendATandWaitOK("AT+CNMI=2,1,0,0,0", 1000)) {
    if (++cnmiRetry >= 3) break;
    logCaptureLn(String("设置CNMI失败，重试..."));
    blink_short();
  }
  if (cnmiRetry < 3) logCaptureLn(String("CNMI参数设置完成（存储模式）"));
  else logCaptureLn(String("⚠️ CNMI设置失败，短信可能无法上报"));
  int cmgfRetry = 0;
  while (!sendATandWaitOK("AT+CMGF=0", 1000)) {
    if (++cmgfRetry >= 3) break;
    logCaptureLn(String("设置PDU模式失败，重试..."));
    blink_short();
  }
  if (cmgfRetry < 3) logCaptureLn(String("PDU模式设置完成"));
  else logCaptureLn(String("⚠️ PDU模式设置失败，短信解析可能异常"));
  int ceregRetry = 0;
  while (!waitCEREG() && ceregRetry < 30) {
    logCaptureLn(String("等待网络注册..."));
    ceregRetry++;
    blink_short();
  }
  if (ceregRetry < 30) {
    logCaptureLn(String("网络已注册"));
    modemReady = true;

    // 注册成功后抓取一次模组静态信息，供 Web 概览展示（单项失败不影响功能）
    String imei = modemQueryImei();
    if (imei.length() > 0) modemImeiCache = imei;

    String ccid = sendATCommand("AT+ICCID", 2000);
    int ci = ccid.indexOf("+ICCID:");
    if (ci >= 0) {
      String tmp = ccid.substring(ci + 7);
      int e = tmp.indexOf('\r');
      if (e < 0) e = tmp.indexOf('\n');
      if (e > 0) tmp = tmp.substring(0, e);
      tmp.trim();
      if (tmp.length() > 0 && tmp.indexOf("ERROR") < 0) modemIccidCache = tmp;
    }

    String op = modemParseCops(sendATCommand("AT+COPS?", 2000));
    if (op.length() > 0) modemOperatorCache = op;

  } else {
    int dbm;
    String csq = sendATCommand("AT+CSQ", 2000);
    String sig = modemParseCsq(csq, dbm) ? String(dbm) + " dBm" : "未知";
    logCaptureLn(String("⚠️ 网络注册超时（SIM已就绪，信号=" + sig + "），稍后自动重试"));
    modemReady = false;
  }
  return modemReady;
}

void blink_short(unsigned long gap_time) {
  digitalWrite(LED_BUILTIN, LOW);
  delay(50);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(gap_time);
}

bool sendATandWaitOK(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < timeout) {
    wdtFeed();
    if (Serial1.available()) {
      char c = Serial1.read();
      if (resp.length() < 4096) resp += c;
      if (resp.indexOf("OK") >= 0) {
        // 终止行后短暂收尾，接住与 OK 几乎同时到达的 URC/PDU。
        unsigned long drainStart = millis();
        while (millis() - drainStart < 50) {
          int budget = 128;
          while (budget-- > 0 && Serial1.available()) {
            char extra = Serial1.read();
            if (resp.length() < 4096) resp += extra;
          }
          server.handleClient();
        }
        smsNoteCmtiFromResponse(resp);
        return true;
      }
      if (resp.indexOf("ERROR") >= 0) {
        unsigned long drainStart = millis();
        while (millis() - drainStart < 50) {
          int budget = 128;
          while (budget-- > 0 && Serial1.available()) {
            char extra = Serial1.read();
            if (resp.length() < 4096) resp += extra;
          }
          server.handleClient();
        }
        smsNoteCmtiFromResponse(resp);
        return false;
      }
    }
    server.handleClient();
  }
  smsNoteCmtiFromResponse(resp);
  return false;
}

// 退出 AT+CMGS 的正文输入态。失败路径若不发 ESC，后续所有 AT 命令都可能被当成短信正文。
static void abortSmsInput() {
  Serial1.write(0x1B);  // ESC
  Serial1.write('\r');
  String tail;
  unsigned long start = millis();
  while (millis() - start < 500) {
    wdtFeed();
    int budget = 128;
    while (budget-- > 0 && Serial1.available()) {
      char c = Serial1.read();
      if (tail.length() < 2048) tail += c;
    }
    server.handleClient();
  }
  smsNoteCmtiFromResponse(tail);
}

// 检测网络注册状态（LTE/4G）
// CEREG状态: 1=已注册本地, 5=已注册漫游
bool waitCEREG() {
  String resp = sendATCommand("AT+CEREG?", 2000);
  int pos = resp.indexOf("+CEREG:");
  if (pos < 0) return false;

  int comma = resp.indexOf(',', pos);
  if (comma < 0) return false;
  int end = resp.indexOf('\r', comma + 1);
  if (end < 0) end = resp.indexOf('\n', comma + 1);
  if (end < 0) end = resp.length();
  String statText = resp.substring(comma + 1, end);
  statText.trim();
  int extraComma = statText.indexOf(',');
  if (extraComma >= 0) statText = statText.substring(0, extraComma);
  int stat = statText.toInt();
  return stat == 1 || stat == 5;
}

// 发送短信（PDU模式）
bool sendSMS(const char* phoneNumber, const char* message) {
  logCaptureLn(String("准备发送短信..."));
  logCapture(String("目标号码: ")); logCaptureLn(String(phoneNumber));
  logCapture(String("短信内容: ")); logCaptureLn(String(message));

  // 使用pdulib编码PDU
  pdu.setSCAnumber();  // 使用默认短信中心
  int pduLen = pdu.encodePDU(phoneNumber, message);
  
  if (pduLen < 0) {
    logCapture(String("PDU编码失败，错误码: "));
    logCaptureLn(String(pduLen));
    return false;
  }
  
  logCapture(String("PDU数据: ")); logCaptureLn(String(pdu.getSMS()));
  logCapture(String("PDU长度: ")); logCaptureLn(String(pduLen));
  
  // 发送AT+CMGS命令
  String cmgsCmd = "AT+CMGS=";
  cmgsCmd += pduLen;
  
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmgsCmd);
  
  // 等待 > 提示符（不再逐字符写日志：模组回显会产生海量小 String 分配并刷爆日志环形缓冲）
  unsigned long start = millis();
  bool gotPrompt = false;
  while (millis() - start < 5000) {
    wdtFeed();
    if (Serial1.available()) {
      char c = Serial1.read();
      if (c == '>') {
        gotPrompt = true;
        break;
      }
    }
    server.handleClient();
  }

  if (!gotPrompt) {
    logCaptureLn(String("未收到>提示符"));
    abortSmsInput();
    return false;
  }

  // 发送PDU数据
  Serial1.print(pdu.getSMS());
  Serial1.write(0x1A);  // Ctrl+Z 结束

  // 等待响应
  start = millis();
  String resp = "";
  resp.reserve(256);
  while (millis() - start < 30000) {
    wdtFeed();
    // 每轮限制读取量，防止模组持续输出时永远困在内层循环，饿死 Web/看门狗。
    int budget = 256;
    while (budget-- > 0 && Serial1.available()) {
      char c = Serial1.read();
      if (resp.length() < 4096) resp += c;
      if (resp.indexOf("+CMGS:") >= 0 && resp.indexOf("OK") >= 0) {
        logCaptureLn(String("短信发送成功"));
        smsNoteCmtiFromResponse(resp);
        return true;
      }
      if (resp.indexOf("ERROR") >= 0) {
        logCaptureLn(String("短信发送失败: ") + resp);
        smsNoteCmtiFromResponse(resp);
        abortSmsInput();
        return false;
      }
    }
    wdtFeed();
    server.handleClient();
  }
  logCaptureLn(String("短信发送超时"));
  smsNoteCmtiFromResponse(resp);
  abortSmsInput();
  return false;
}
