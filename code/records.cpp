#include "records.h"
#include "push.h"    // jsonEscape
#include <LittleFS.h>

static SmsRecord recs[SMS_RECORD_MAX];
static int recHead = 0;   // 下一个写入位置
static int recCount = 0;

// ---- LittleFS 持久化 ----
// 行格式（TSV）：sender \t text \t timestamp \t emailOk \t pushMask \t pushEnabled
// 字段内的 tab/换行替换为空格（展示型数据，无损要求低）
static const size_t RECORDS_ROTATE = 200 * 1024;  // 200KB 轮转

static String sanitizeField(const String& s) {
  String r = s;
  for (unsigned int i = 0; i < r.length(); i++) {
    char c = r.charAt(i);
    if (c == '\t' || c == '\r' || c < 0x20) r.setCharAt(i, ' ');
  }
  if (r.length() > 200) r = r.substring(0, 197) + "...";
  return r;
}

static void persistRecord(const SmsRecord& r) {
  File f = LittleFS.open(RECORDS_FILE, FILE_APPEND);
  if (!f) {
    return;  // 持久化失败不影响内存记录
  }
  String line = sanitizeField(r.sender) + "\t" + sanitizeField(r.text) + "\t" +
                sanitizeField(r.timestamp) + "\t" + String(r.emailOk ? 1 : 0) + "\t" +
                String(r.pushMask) + "\t" + String(r.pushEnabled);
  f.println(line);
  f.close();

  // 轮转：超过上限把当前文件降级为 .1（覆盖更老的），从头开始
  File sz = LittleFS.open(RECORDS_FILE, "r");
  bool needRotate = sz && sz.size() > RECORDS_ROTATE;
  if (sz) sz.close();
  if (needRotate) {
    LittleFS.remove(RECORDS_OLD);
    LittleFS.rename(RECORDS_FILE, RECORDS_OLD);
  }
}

void recordsAdd(const char* sender, const char* text, const char* timestamp,
                bool emailOk, uint8_t pushMask, uint8_t pushEnabled) {
  SmsRecord& r = recs[recHead];
  r.valid = true;
  // 截断防内存膨胀：50 条 × 长短信全文（可达 700B）在 recordsJson 序列化时
  // 连同转义副本峰值可吃掉 100KB+ 堆；记录列表 160 字符足够展示
  r.sender = String(sender).substring(0, 24);
  r.text = String(text).substring(0, 160);
  // PDU 网络时间戳为空时用设备运行时长兜底，至少能看出先后顺序
  if (timestamp == nullptr || timestamp[0] == 0) {
    long up = millis() / 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "+%ld:%02ld:%02ld", up / 3600, (up % 3600) / 60, up % 60);
    r.timestamp = buf;
  } else {
    r.timestamp = String(timestamp).substring(0, 24);
  }
  r.emailOk = emailOk;
  r.pushMask = pushMask;
  r.pushEnabled = pushEnabled;
  recHead = (recHead + 1) % SMS_RECORD_MAX;
  if (recCount < SMS_RECORD_MAX) recCount++;

  persistRecord(r);
}

// 启动时把持久化文件读回内存环形缓冲（自动只留最新 SMS_RECORD_MAX 条）
void recordsLoad() {
  for (int pass = 0; pass < 2; pass++) {
    const char* path = (pass == 0) ? RECORDS_OLD : RECORDS_FILE;  // 旧文件先读（更早）
    if (!LittleFS.exists(path)) continue;
    File f = LittleFS.open(path, "r");
    if (!f) continue;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      // 解析 6 个 TSV 字段
      String fields[6];
      int fi = 0, start = 0;
      while (fi < 5) {
        int tab = line.indexOf('\t', start);
        if (tab < 0) break;
        fields[fi++] = line.substring(start, tab);
        start = tab + 1;
      }
      fields[fi] = line.substring(start);
      if (fi < 5) continue;  // 字段不足，跳过损坏行

      SmsRecord& r = recs[recHead];
      r.valid = true;
      r.sender = fields[0];
      r.text = fields[1];
      r.timestamp = fields[2];
      r.emailOk = (fields[3].toInt() != 0);
      r.pushMask = (uint8_t)fields[4].toInt();
      r.pushEnabled = (uint8_t)fields[5].toInt();
      recHead = (recHead + 1) % SMS_RECORD_MAX;
      if (recCount < SMS_RECORD_MAX) recCount++;
    }
    f.close();
  }
}

int recordsCount() {
  return recCount;
}

void recordsClear() {
  for (int i = 0; i < SMS_RECORD_MAX; i++) {
    recs[i].valid = false;
    recs[i].sender = "";
    recs[i].text = "";
    recs[i].timestamp = "";
  }
  recHead = 0;
  recCount = 0;
  LittleFS.remove(RECORDS_FILE);
  LittleFS.remove(RECORDS_OLD);
}

String recordsJson() {
  String json = "{\"total\":" + String(recCount) + ",\"items\":[";
  // 最新在前：从最近写入的一条倒序遍历
  for (int k = 0; k < recCount; k++) {
    int idx = (recHead - 1 - k + SMS_RECORD_MAX) % SMS_RECORD_MAX;
    if (!recs[idx].valid) continue;
    if (k > 0) json += ",";
    json += "{\"s\":\"" + jsonEscape(recs[idx].sender) + "\",";
    json += "\"t\":\"" + jsonEscape(recs[idx].text) + "\",";
    json += "\"ts\":\"" + jsonEscape(recs[idx].timestamp) + "\",";
    json += "\"e\":" + String(recs[idx].emailOk ? "1" : "0") + ",";
    json += "\"p\":" + String(recs[idx].pushMask) + ",";
    json += "\"en\":" + String(recs[idx].pushEnabled) + "}";
  }
  json += "]}";
  return json;
}
