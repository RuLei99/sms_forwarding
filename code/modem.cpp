#include "modem.h"
#include "web_handlers.h"

// 发送AT命令并获取响应
String sendATCommand(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < timeout) {
    if (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0) {
        // 读取剩余数据（最多 50ms）
        unsigned long t = millis();
        while (millis() - t < 50) {
          if (Serial1.available()) resp += (char)Serial1.read();
          server.handleClient();
        }
        return resp;
      }
    }
    server.handleClient();
  }
  return resp;
}

// 新增"模组断电重启"函数
void modemPowerCycle() {
  pinMode(MODEM_EN_PIN, OUTPUT);

  logCaptureLn(String("EN 拉低：关闭模组"));
  digitalWrite(MODEM_EN_PIN, LOW);
  delay(1200);  // 关机时间给够

  logCaptureLn(String("EN 拉高：开启模组"));
  digitalWrite(MODEM_EN_PIN, HIGH);
  delay(6000);  // 等模组完全启动再发AT（关键）
}

// 重启模组（EN引脚断电重启 + 重新初始化）
// 内部占用 Serial1 期间会嵌套 server.handleClient()，必须持有 modemPortBusy，
// 否则嵌套进来的请求会与初始化 AT 序列抢串口（setup/后台重试路径同样受益）
void resetModule() {
  modemPortBusy = true;
  logCaptureLn(String("正在硬重启模组（EN 断电重启）..."));
  modemPowerCycle();
  modemInit();
  modemPortBusy = false;
}

// 模组 AT 初始化流程（setup 中调用，resetModule 后也调用）
// 返回是否初始化成功（网络已注册）。失败时置 modemReady=false，
// 不再无限重试 —— 网页保持可用，后台由 healthTask 周期性重试
static bool modemInitInner();  // 实际初始化流程（由 modemInit 包上串口互斥后调用）

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
  int atRetry = 0;
  while (!sendATandWaitOK("AT", 1000)) {
    if (++atRetry >= 20) {
      logCaptureLn(String("⚠️ 模组无响应（已尝试20次），放弃初始化，稍后自动重试"));
      return false;
    }
    logCaptureLn(String("AT未响应，重试..."));
    blink_short();
  }
  logCaptureLn(String("模组AT响应正常"));

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
  while (!sendATandWaitOK("AT+CNMI=2,2,0,0,0", 1000)) {
    if (++cnmiRetry >= 3) break;
    logCaptureLn(String("设置CNMI失败，重试..."));
    blink_short();
  }
  if (cnmiRetry < 3) logCaptureLn(String("CNMI参数设置完成"));
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
    if (config.smsOnly) {
      // LTE附着后网络可能自动重建默认承载，再补一次去激活，确保零流量
      // ML307Y 对 CGACT 命令有兼容问题（见上方 need_set_CGACT 判断），只能跳过
      if (!need_set_CGACT) {
        logCaptureLn(String("⚠️ 仅收短信模式：该型号无法主动去激活数据承载，注意流量消耗"));
      } else if (sendATandWaitOK("AT+CGACT=0,1", 5000)) {
        logCaptureLn(String("仅收短信模式：已再次去激活数据承载(AT+CGACT=0,1)"));
      } else {
        logCaptureLn(String("仅收短信模式：数据承载去激活失败（不影响收短信）"));
      }
    }
  } else {
    logCaptureLn(String("⚠️ 网络注册超时（无SIM卡或信号差），模组功能不可用，稍后自动重试"));
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
    if (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("OK") >= 0) return true;
      if (resp.indexOf("ERROR") >= 0) return false;
    }
    server.handleClient();
  }
  return false;
}

// 检测网络注册状态（LTE/4G）
// CEREG状态: 1=已注册本地, 5=已注册漫游
bool waitCEREG() {
  Serial1.println("AT+CEREG?");
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < 2000) {
    if (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("+CEREG:") >= 0) {
        if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
        if (resp.indexOf(",0") >= 0 || resp.indexOf(",2") >= 0 || 
            resp.indexOf(",3") >= 0 || resp.indexOf(",4") >= 0) return false;
      }
    }
    server.handleClient();
  }
  return false;
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
  
  // 等待 > 提示符
  unsigned long start = millis();
  bool gotPrompt = false;
  while (millis() - start < 5000) {
    if (Serial1.available()) {
      char c = Serial1.read();
      logCapture(String(c));
      if (c == '>') {
        gotPrompt = true;
        break;
      }
    }
    server.handleClient();
  }
  
  if (!gotPrompt) {
    logCaptureLn(String("未收到>提示符"));
    return false;
  }
  
  // 发送PDU数据
  Serial1.print(pdu.getSMS());
  Serial1.write(0x1A);  // Ctrl+Z 结束
  
  // 等待响应
  start = millis();
  String resp = "";
  while (millis() - start < 30000) {
    while (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      logCapture(String(c));
      if (resp.indexOf("OK") >= 0) {
        logCaptureLn(String("\n短信发送成功"));
        return true;
      }
      if (resp.indexOf("ERROR") >= 0) {
        logCaptureLn(String("\n短信发送失败"));
        return false;
      }
    }
    server.handleClient();
  }
  logCaptureLn(String("短信发送超时"));
  return false;
}
