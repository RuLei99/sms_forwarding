#include "sms_process.h"
#include "web_handlers.h"
#include "modem.h"
#include "push.h"
#include "records.h"
#include "config.h"

// 关键词过滤：返回 true 表示该短信应被拦截
static bool blockedByKeywordFilter(const char* text) {
  if (config.filterKeywords.length() == 0) return false;

  String content = String(text);
  // 逐行取关键词判断命中
  int listLen = (int)config.filterKeywords.length();
  bool hit = false;
  int start = 0;
  while (start <= listLen && !hit) {
    int end = config.filterKeywords.indexOf('\n', start);
    if (end == -1) end = listLen;
    String kw = config.filterKeywords.substring(start, end);
    kw.trim();
    if (kw.length() > 0 && content.indexOf(kw) >= 0) hit = true;
    start = end + 1;
  }
  // 白名单模式：命中才转发（未命中=拦截）；黑名单模式：命中即拦截
  return config.filterWhitelist ? !hit : hit;
}

// 初始化长短信缓存
void initConcatBuffer() {
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    concatBuffer[i].inUse = false;
    concatBuffer[i].receivedParts = 0;
    for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
      concatBuffer[i].parts[j].valid = false;
      concatBuffer[i].parts[j].text = "";
    }
  }
}

// 查找或创建长短信缓存槽位
int findOrCreateConcatSlot(int refNumber, const char* sender, int totalParts) {
  // 先查找是否已存在
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].inUse && 
        concatBuffer[i].refNumber == refNumber &&
        concatBuffer[i].sender.equals(sender)) {
      return i;
    }
  }
  
  // 查找空闲槽位
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (!concatBuffer[i].inUse) {
      concatBuffer[i].inUse = true;
      concatBuffer[i].refNumber = refNumber;
      concatBuffer[i].sender = String(sender);
      concatBuffer[i].totalParts = totalParts;
      concatBuffer[i].receivedParts = 0;
      concatBuffer[i].firstPartTime = millis();
      for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
        concatBuffer[i].parts[j].valid = false;
        concatBuffer[i].parts[j].text = "";
      }
      return i;
    }
  }
  
  // 没有空闲槽位，查找最老的槽位覆盖
  int oldestSlot = 0;
  unsigned long oldestTime = concatBuffer[0].firstPartTime;
  for (int i = 1; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].firstPartTime < oldestTime) {
      oldestTime = concatBuffer[i].firstPartTime;
      oldestSlot = i;
    }
  }
  
  // 覆盖最老的槽位
  logCaptureLn(String("⚠️ 长短信缓存已满，覆盖最老的槽位"));
  concatBuffer[oldestSlot].inUse = true;
  concatBuffer[oldestSlot].refNumber = refNumber;
  concatBuffer[oldestSlot].sender = String(sender);
  concatBuffer[oldestSlot].totalParts = totalParts;
  concatBuffer[oldestSlot].receivedParts = 0;
  concatBuffer[oldestSlot].firstPartTime = millis();
  for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
    concatBuffer[oldestSlot].parts[j].valid = false;
    concatBuffer[oldestSlot].parts[j].text = "";
  }
  return oldestSlot;
}

// 合并长短信各分段
String assembleConcatSms(int slot) {
  String result = "";
  for (int i = 0; i < concatBuffer[slot].totalParts; i++) {
    if (concatBuffer[slot].parts[i].valid) {
      result += concatBuffer[slot].parts[i].text;
    } else {
      result += "[缺失分段" + String(i + 1) + "]";
    }
  }
  return result;
}

// 清空长短信槽位
void clearConcatSlot(int slot) {
  concatBuffer[slot].inUse = false;
  concatBuffer[slot].receivedParts = 0;
  concatBuffer[slot].sender = "";
  concatBuffer[slot].timestamp = "";
  for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
    concatBuffer[slot].parts[j].valid = false;
    concatBuffer[slot].parts[j].text = "";
  }
}

// 检查长短信超时并转发
void checkConcatTimeout() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].inUse) {
      if (now - concatBuffer[i].firstPartTime >= CONCAT_TIMEOUT_MS) {
        logCaptureLn(String("⏰ 长短信超时，强制转发不完整消息"));
        logCaptureF("  参考号: %d, 已收到: %d/%d\n", 
                      concatBuffer[i].refNumber,
                      concatBuffer[i].receivedParts,
                      concatBuffer[i].totalParts);
        
        // 合并已收到的分段
        String fullText = assembleConcatSms(i);
        
        // 处理短信内容
        processSmsContent(concatBuffer[i].sender.c_str(), 
                         fullText.c_str(), 
                         concatBuffer[i].timestamp.c_str());
        
        // 清空槽位
        clearConcatSlot(i);
      }
    }
  }
}

// 读取串口一行（含回车换行），返回行字符串，无新行时返回空
// 内部使用跨调用静态缓冲，非重入 —— 仅供本文件 checkSerial1URC 使用
static String readSerialLine(HardwareSerial& port) {
  static char lineBuf[SERIAL_BUFFER_SIZE];
  static int linePos = 0;
  static bool overflow = false;

  while (port.available()) {
    char c = port.read();
    if (c == '\n') {
      String res;
      if (overflow) {
        // 丢弃整条超长行：半截内容若被当作 PDU 解析，可能拼出错误短信
        overflow = false;
        res = "<LINE_OVERFLOW_DROPPED>";
      } else {
        lineBuf[linePos] = 0;
        res = String(lineBuf);
      }
      linePos = 0;
      return res;
    } else if (c != '\r') {  // 跳过\r
      if (linePos < SERIAL_BUFFER_SIZE - 1) {
        lineBuf[linePos++] = c;
      } else {
        overflow = true;  // 缓冲已满：停止写入，丢弃本行剩余内容直到换行
      }
    }
  }
  return "";
}

// 检查字符串是否为有效的十六进制PDU数据
bool isHexString(const String& str) {
  if (str.length() == 0) return false;
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

// 检查发送者是否在号码黑名单中
bool isInNumberBlackList(const char* sender) {
  if (config.numberBlackList.length() == 0) return false;

  String originalSender = String(sender);
  bool has86 = originalSender.startsWith("+86");
  String strippedSender = has86 ? originalSender.substring(3) : "";

  int listLen = (int)config.numberBlackList.length();

  int start = 0;
  while (start <= listLen) {
    int end = config.numberBlackList.indexOf('\n', start);
    if (end == -1) end = listLen;

    String line = config.numberBlackList.substring(start, end);
    line.trim();

    if (line.length() > 0 && (line.equals(originalSender) || (has86 && line.equals(strippedSender)))) {
      return true;
    }

    start = end + 1;
  }

  return false;
}

// 检查发送者是否为管理员
bool isAdmin(const char* sender) {
  if (config.adminPhone.length() == 0) return false;
  
  // 去除可能的国际区号前缀进行比较
  String senderStr = String(sender);
  String adminStr = config.adminPhone;
  
  // 去除+86前缀
  if (senderStr.startsWith("+86")) {
    senderStr = senderStr.substring(3);
  }
  if (adminStr.startsWith("+86")) {
    adminStr = adminStr.substring(3);
  }
  
  return senderStr.equals(adminStr);
}

// 处理管理员命令
void processAdminCommand(const char* sender, const char* text) {
  String cmd = String(text);
  cmd.trim();

  logCaptureLn(String("处理管理员命令: " + cmd));

  // 管理员命令经 SMS 通道直接操作模组串口；Web 端长 AT 操作（Ping/重启等）进行中时
  // 直接执行会与嵌套 handleClient 抢串口导致指令交错，忙时拒绝并邮件告知
  if (modemPortBusy) {
    logCaptureLn(String("⚠️ 模组串口忙，管理员命令暂缓"));
    String body = "模组正忙（Web 端操作进行中），请稍后重发命令: " + cmd;
    sendEmailNotification("命令执行失败", body.c_str());
    return;
  }

  // 处理 SMS:号码:内容 命令
  if (cmd.startsWith("SMS:")) {
    int firstColon = cmd.indexOf(':');
    int secondColon = cmd.indexOf(':', firstColon + 1);
    
    if (secondColon > firstColon + 1) {
      String targetPhone = cmd.substring(firstColon + 1, secondColon);
      String smsContent = cmd.substring(secondColon + 1);
      
      targetPhone.trim();
      smsContent.trim();
      
      logCaptureLn(String("目标号码: " + targetPhone));
      logCaptureLn(String("短信内容: " + smsContent));
      
      bool success = sendSMS(targetPhone.c_str(), smsContent.c_str());
      
      // 发送邮件通知结果
      String subject = success ? "短信发送成功" : "短信发送失败";
      String body = "管理员命令执行结果:\n";
      body += "命令: " + cmd + "\n";
      body += "目标号码: " + targetPhone + "\n";
      body += "短信内容: " + smsContent + "\n";
      body += "执行结果: " + String(success ? "成功" : "失败");
      
      sendEmailNotification(subject.c_str(), body.c_str());
    } else {
      logCaptureLn(String("SMS命令格式错误"));
      sendEmailNotification("命令执行失败", "SMS命令格式错误，正确格式: SMS:号码:内容");
    }
  }
  // 处理 RESET 命令
  else if (cmd.equals("RESET")) {
    logCaptureLn(String("执行RESET命令"));
    
    // 先发送邮件通知（因为重启后就发不了了）
    sendEmailNotification("重启命令已执行", "收到RESET命令，即将重启模组和ESP32...");
    
    // 重启模组
    resetModule();
    
    // 重启ESP32
    logCaptureLn(String("正在重启ESP32..."));
    delay(1000);
    ESP.restart();
  }
  else {
    logCaptureLn(String("未知命令: " + cmd));
  }
}

// 处理最终的短信内容（管理员命令检查和转发）
void processSmsContent(const char* sender, const char* text, const char* timestamp) {
  logCaptureLn(String("=== 处理短信内容 ==="));
  logCaptureLn(String("发送者: " + String(sender)));
  logCaptureLn(String("时间戳: " + String(timestamp)));
  logCaptureLn(String("内容: " + String(text)));
  logCaptureLn(String("===================="));

  // 检查是否在号码黑名单中
  if (isInNumberBlackList(sender)) {
    logCaptureLn(String("发送者在号码黑名单中，忽略该短信"));
    return;
  }

  // 检查是否为管理员命令（管理员命令不受关键词过滤影响）
  if (isAdmin(sender)) {
    logCaptureLn(String("收到管理员短信，检查命令..."));
    String smsText = String(text);
    smsText.trim();

    // 检查是否为命令格式
    if (smsText.startsWith("SMS:") || smsText.equals("RESET")) {
      processAdminCommand(sender, text);
      // 命令已处理，不再发送普通通知邮件
      return;
    }
  }

  // 关键词过滤（白名单/黑名单模式）
  if (blockedByKeywordFilter(text)) {
    logCaptureLn(String(config.filterWhitelist
      ? "短信未命中白名单关键词，不转发"
      : "短信命中黑名单关键词，不转发"));
    return;
  }

  // 发送通知http（推送到所有启用的通道），位图记录各通道结果
  uint8_t pushMask = sendSMSToServer(sender, text, timestamp);
  uint8_t pushEnabled = 0;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) pushEnabled |= (1 << i);
  }
  // 发送通知邮件
  String subject = ""; subject+="短信";subject+=sender;subject+=",";subject+=text;
  String body = ""; body+="来自：";body+=sender;body+="，时间：";body+=timestamp;body+="，内容：";body+=text;
  bool emailOk = sendEmailNotification(subject.c_str(), body.c_str());

  // 落一条转发记录（含各通道结果）
  recordsAdd(sender, text, timestamp, emailOk, pushMask, pushEnabled);
}

// 解码并处理一行完整 PDU。直接上报（+CMT）和存储上报（+CMTI/+CMGR）共用。
static bool processPduLine(const String& line) {
  if (!isHexString(line)) return false;

  logCaptureLn(String("收到PDU数据，长度: " + String(line.length()) + " 字符"));
  if (!pdu.decodePDU(line.c_str())) {
    logCaptureLn(String("❌ PDU解析失败！"));
    return false;
  }

  logCaptureLn(String("✓ PDU解析成功"));
  logCaptureLn(String("=== 短信内容 ==="));
  logCaptureLn(String("发送者: " + String(pdu.getSender())));
  logCaptureLn(String("时间戳: " + String(pdu.getTimeStamp())));
  logCaptureLn(String("内容: " + String(pdu.getText())));

  int* concatInfo = pdu.getConcatInfo();
  int refNumber = concatInfo[0];
  int partNumber = concatInfo[1];
  int totalParts = concatInfo[2];
  logCaptureF("长短信信息: 参考号=%d, 当前=%d, 总计=%d\n", refNumber, partNumber, totalParts);
  logCaptureLn(String("==============="));

  if (totalParts > 1 && partNumber > 0) {
    logCaptureF("📧 收到长短信分段 %d/%d\n", partNumber, totalParts);
    int slot = findOrCreateConcatSlot(refNumber, pdu.getSender(), totalParts);
    int partIndex = partNumber - 1;
    if (partIndex < 0 || partIndex >= MAX_CONCAT_PARTS) {
      logCaptureLn(String("❌ 长短信分段编号超出支持范围"));
      return false;
    }

    if (!concatBuffer[slot].parts[partIndex].valid) {
      concatBuffer[slot].parts[partIndex].valid = true;
      concatBuffer[slot].parts[partIndex].text = String(pdu.getText());
      concatBuffer[slot].receivedParts++;
      if (concatBuffer[slot].receivedParts == 1) {
        concatBuffer[slot].timestamp = String(pdu.getTimeStamp());
      }
      logCaptureF("  已缓存分段 %d，当前已收到 %d/%d\n",
                  partNumber, concatBuffer[slot].receivedParts, totalParts);
    } else {
      logCaptureF("  ⚠️ 分段 %d 已存在，跳过\n", partNumber);
    }

    if (concatBuffer[slot].receivedParts >= totalParts) {
      logCaptureLn(String("✅ 长短信已收齐，开始合并转发"));
      String fullText = assembleConcatSms(slot);
      processSmsContent(concatBuffer[slot].sender.c_str(), fullText.c_str(),
                        concatBuffer[slot].timestamp.c_str());
      clearConcatSlot(slot);
    }
  } else {
    processSmsContent(pdu.getSender(), pdu.getText(), pdu.getTimeStamp());
  }
  return true;
}

// ---- 存储短信待读队列 ----
// 发短信/CMGR/后台任务等 AT 命令执行期间到达的 +CMTI 会被当作响应噪声读进 resp，
// 这里从 AT 响应文本中捞回索引，串口空闲时补读，避免存储短信滞留 SIM 无法转发。
#define PENDING_CMTI_MAX 16
static int s_pendingCmti[PENDING_CMTI_MAX];
static int s_pendingCmtiCount = 0;
#define PENDING_PDU_MAX 4
static String s_pendingPdu[PENDING_PDU_MAX];
static int s_pendingPduCount = 0;

static void noteCmtiIndex(int index) {
  if (index < 0 || index > 9999) return;
  for (int i = 0; i < s_pendingCmtiCount; i++) {
    if (s_pendingCmti[i] == index) return;  // 同一索引未读前只排一次
  }
  if (s_pendingCmtiCount >= PENDING_CMTI_MAX) {
    logCaptureLn(String("⚠️ 待读短信索引队列已满，丢弃: " + String(index)));
    return;
  }
  s_pendingCmti[s_pendingCmtiCount++] = index;
}

static void noteCapturedPdu(const String& line) {
  if (line.length() < 20 || !isHexString(line)) return;
  if (s_pendingPduCount >= PENDING_PDU_MAX) {
    logCaptureLn(String("⚠️ 待处理直推短信队列已满，丢弃一条PDU"));
    return;
  }
  s_pendingPdu[s_pendingPduCount++] = line;
}

// 从 AT 响应文本中提取全部 +CMTI 存储索引及 +CMT 直推 PDU。
// 后者不能在 AT 调用栈内立即转发（可能再次使用模组），先排队到主循环处理。
void smsNoteCmtiFromResponse(const String& resp) {
  int pos = 0;
  while ((pos = resp.indexOf("+CMTI:", pos)) >= 0) {
    int lineEnd = resp.indexOf('\n', pos);
    if (lineEnd < 0) lineEnd = resp.length();
    String line = resp.substring(pos, lineEnd);
    int comma = line.lastIndexOf(',');
    if (comma >= 0) {
      noteCmtiIndex(line.substring(comma + 1).toInt());
    }
    pos = lineEnd + 1;
  }

  int start = 0;
  bool waitPdu = false;
  while (start <= (int)resp.length()) {
    int end = resp.indexOf('\n', start);
    if (end < 0) end = resp.length();
    String line = resp.substring(start, end);
    line.trim();
    if (line.startsWith("+CMT:")) {
      waitPdu = true;
    } else if (waitPdu && line.length() >= 20 && isHexString(line)) {
      noteCapturedPdu(line);
      waitPdu = false;
    }
    start = end + 1;
  }
}

// ML307R 的部分固件会把 AT+CNMI=2,2 规范化为 2,1，此时只上报
// +CMTI: "SM",<index>，短信正文需要再用 AT+CMGR=<index> 读取。

// ---- 毒短信保护 ----
// 兜底扫描会反复捞起存储里的短信，解析连续失败 3 次的索引直接删除，
// 避免坏 PDU / 已被 URC 路径读走的幽灵索引每 10 秒重试一次造成死循环。
#define READ_FAIL_MAX 8
static struct { int index; uint8_t fails; } s_readFails[READ_FAIL_MAX];

static int bumpReadFail(int index) {
  for (int i = 0; i < READ_FAIL_MAX; i++) {
    if (s_readFails[i].index == index) return ++s_readFails[i].fails;
  }
  for (int i = 0; i < READ_FAIL_MAX; i++) {
    if (s_readFails[i].index == 0) {
      s_readFails[i].index = index;
      s_readFails[i].fails = 1;
      return 1;
    }
  }
  return 99;  // 表满：按超限处理，避免旧毒短信永远占着扫描周期
}

static void clearReadFail(int index) {
  for (int i = 0; i < READ_FAIL_MAX; i++) {
    if (s_readFails[i].index == index) s_readFails[i] = {0, 0};
  }
}

static void dropPoisonSms(int index) {
  logCaptureLn(String("⚠️ 索引 " + String(index) + " 连续3次无法解析，删除该条避免扫描死循环"));
  modemPortBusy = true;
  sendATCommand(("AT+CMGD=" + String(index)).c_str(), 3000);
  modemPortBusy = false;
  clearReadFail(index);
}

static bool readStoredSms(int index) {
  if (index < 0 || index > 9999) return false;
  if (modemPortBusy) {
    // 串口被长 AT 操作占用：入队延后补读，不丢
    noteCmtiIndex(index);
    logCaptureLn(String("⏳ 串口忙，存储短信延后读取，索引=" + String(index)));
    return false;
  }

  modemPortBusy = true;
  String cmd = "AT+CMGR=" + String(index);
  String resp = sendATCommand(cmd.c_str(), 5000);
  modemPortBusy = false;

  int start = 0;
  while (start <= (int)resp.length()) {
    int end = resp.indexOf('\n', start);
    if (end < 0) end = resp.length();
    String line = resp.substring(start, end);
    line.trim();
    // 短信 PDU 至少包含短信中心、地址和 TPDU；过滤掉短的纯数字状态行。
    if (line.length() >= 20 && isHexString(line)) {
      if (!processPduLine(line)) {
        if (bumpReadFail(index) >= 3) dropPoisonSms(index);
        return false;
      }
      clearReadFail(index);

      // 已成功持久化到 Web 短信记录后删除模组存储副本，避免 SIM 存储写满。
      modemPortBusy = true;
      String delCmd = "AT+CMGD=" + String(index);
      String delResp = sendATCommand(delCmd.c_str(), 3000);
      modemPortBusy = false;
      if (delResp.indexOf("OK") < 0) {
        logCaptureLn(String("⚠️ 已处理短信，但删除模组存储副本失败，索引=" + String(index)));
      }
      return true;
    }
    start = end + 1;
  }

  logCaptureLn(String("❌ CMGR未返回有效PDU，索引=" + String(index)));
  if (bumpReadFail(index) >= 3) dropPoisonSms(index);
  return false;
}

// ML307R-DC 的 URC（+CMTI 通知/+CMT 直推）会被模组发射时的串口噪声破坏
// （吞头/乱码洪水），但短信仍正常写入存储。定时扫描存储作为兜底，不依赖任何 URC。
static void scanStoredSms() {
  if (modemPortBusy) return;

  modemPortBusy = true;
  String resp = sendATCommand("AT+CMGL=4", 5000);  // PDU 模式：列出全部存储短信
  modemPortBusy = false;

  int pos = 0;
  int found = 0;
  while ((pos = resp.indexOf("+CMGL:", pos)) >= 0) {
    int comma = resp.indexOf(',', pos);
    if (comma < 0) break;
    String indexText = resp.substring(pos + 6, comma);
    indexText.trim();
    int index = indexText.toInt();
    if (index > 0) {
      noteCmtiIndex(index);
      found++;
    }
    pos = comma + 1;
  }
  if (found > 0) {
    logCaptureLn(String("兜底扫描发现存储短信: " + String(found) + " 条"));
  }
}

// 处理URC和PDU
void checkSerial1URC() {
  static enum { IDLE, WAIT_PDU } state = IDLE;
  static unsigned long lastStoredScan = 0;

  // 每 10 秒扫描一次模组存储：URC 被噪声吃掉时由此补收，转发后自动删除
  if (modemReady && !modemPortBusy && millis() - lastStoredScan >= 10000UL) {
    lastStoredScan = millis();
    scanStoredSms();
  }

  // AT 命令响应中截获的直推短信必须等串口释放后再解码/转发。
  if (s_pendingPduCount > 0 && !modemPortBusy) {
    String pduLine = s_pendingPdu[0];
    for (int i = 1; i < s_pendingPduCount; i++) s_pendingPdu[i - 1] = s_pendingPdu[i];
    s_pendingPduCount--;
    logCaptureLn(String("处理 AT 忙期间到达的直推短信"));
    processPduLine(pduLine);
  }

  // 优先补读 AT 忙期间入队的存储短信（队列仅在串口忙时增长，此时必已空闲）
  if (s_pendingCmtiCount > 0 && !modemPortBusy) {
    int index = s_pendingCmti[0];
    for (int i = 1; i < s_pendingCmtiCount; i++) s_pendingCmti[i - 1] = s_pendingCmti[i];
    s_pendingCmtiCount--;
    logCaptureLn(String("补读忙期间到达的存储短信，索引=" + String(index)));
    readStoredSms(index);
  }

  String line = readSerialLine(Serial1);
  if (line.length() == 0) return;

  if (line == "<LINE_OVERFLOW_DROPPED>") {
    // 噪声洪水会连续丢弃大量行：限流打印，避免刷爆 120 行日志环形缓冲挤掉有用信息
    static unsigned long lastOverflowLog = 0;
    if (millis() - lastOverflowLog >= 5000) {
      lastOverflowLog = millis();
      logCaptureLn(String("⚠️ 模组串口行超过缓冲上限（噪声洪水），已丢弃并重置接收状态"));
    }
    state = IDLE;
    return;
  }

  // 空行（纯 \r\n 噪声）不打日志
  if (line.length() > 0) {
    logCaptureLn(String("Debug> " + line));
  }

  // 存储后通知模式。格式通常为 +CMTI: "SM",3 或 +CMTI: "ME",3。
  if (line.startsWith("+CMTI:")) {
    int comma = line.lastIndexOf(',');
    int index = comma >= 0 ? line.substring(comma + 1).toInt() : -1;
    if (comma < 0 || index < 0) {
      logCaptureLn(String("❌ 无法解析+CMTI存储索引: " + line));
    } else {
      logCaptureLn(String("检测到+CMTI，读取存储短信，索引=" + String(index)));
      readStoredSms(index);
    }
    state = IDLE;
    return;
  }

  if (state == IDLE) {
    if (line.startsWith("+CMT:")) {
      logCaptureLn(String("检测到+CMT，等待PDU数据..."));
      state = WAIT_PDU;
      return;
    }
    // ML307R-DC 直推会吞掉 "+CMT:" 头、只吐裸 PDU 行（短信不落存储、URC 残缺，
    // 实测会静默丢短信）：够长的纯十六进制行直接按直推短信处理
    if (line.length() >= 20 && isHexString(line)) {
      logCaptureLn(String("检测到无头直推PDU，直接处理"));
      processPduLine(line);
    }
    return;
  }

  if (isHexString(line)) {
    processPduLine(line);
    state = IDLE;
    return;
  }

  // 连续到达的两条短信：上一条 +CMT: 后紧跟新的 +CMT: 时继续等待新 PDU。
  if (line.startsWith("+CMT:")) {
    logCaptureLn(String("⚠️ 上一条短信缺少PDU，检测到新的+CMT，继续等待"));
    return;
  }
  logCaptureLn(String("收到非PDU数据，返回IDLE状态"));
  state = IDLE;
}
